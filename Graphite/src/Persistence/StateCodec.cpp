#include "Persistence/StateCodec.h"

#include "Core/Base/JsonFile.h"
#include "Core/Base/Strings.h"
#include "Core/FileSystem/FileSystem.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace Graphite {

	namespace {
		constexpr std::string_view kWindowX = "window.x";
		constexpr std::string_view kWindowY = "window.y";
		constexpr std::string_view kWindowWidth = "window.width";
		constexpr std::string_view kWindowHeight = "window.height";
		constexpr std::string_view kWindowMaximized = "window.maximized";

		constexpr std::string_view kPanelPrefix = "panel.";
		constexpr std::string_view kPanelSuffix = ".visible";

		constexpr std::string_view kRestorePrefix = "restore.";
		constexpr std::string_view kTabCount = "restore.tab.count";
		constexpr std::string_view kActive = "restore.active";

		constexpr std::string_view kSessionCount = "restore.session.count";
		constexpr std::string_view kDiffCount = "restore.diff.count";
		constexpr std::string_view kTerminalCount = "restore.terminal.count";
		constexpr std::string_view kReviewCount = "restore.review.count";

		// More tabs than anybody keeps open, and a ceiling on what a damaged count can cost.
		// Sessions, diffs and terminals are windows of the same kind and share it.
		constexpr std::size_t kMaximumTabs = 256;

		// The words each enum is written as. Written out rather than numbered, so reordering an
		// enum in a later version cannot quietly turn one saved value into another.
		constexpr std::pair<SessionTab, std::string_view> kSessionTabNames[] = {
			{SessionTab::Conversation, "conversation"},
			{SessionTab::Cost, "cost"},
			{SessionTab::Requests, "requests"},
		};

		constexpr std::pair<Core::DiffTarget, std::string_view> kDiffTargetNames[] = {
			{Core::DiffTarget::WorkTreeToIndex, "unstaged"},
			{Core::DiffTarget::IndexToHead, "staged"},
			{Core::DiffTarget::WorkTreeToHead, "working"},
			{Core::DiffTarget::CommitToParent, "commit"},
			{Core::DiffTarget::RevisionToRevision, "range"},
		};

		// "<prefix>.<index>.<field>": the key of one field of one entry in a numbered list.
		std::string IndexedKey(std::string_view prefix, std::size_t index, std::string_view field)
		{
			return std::string{prefix} + "." + std::to_string(index) + "." + std::string{field};
		}

		std::string TabKey(std::size_t index, std::string_view field)
		{
			return IndexedKey("restore.tab", index, field);
		}

		std::string SessionKey(std::size_t index, std::string_view field)
		{
			return IndexedKey("restore.session", index, field);
		}

		std::string DiffKey(std::size_t index, std::string_view field)
		{
			return IndexedKey("restore.diff", index, field);
		}

		std::string TerminalKey(std::size_t index, std::string_view field)
		{
			return IndexedKey("restore.terminal", index, field);
		}

		std::string ReviewTabKey(std::size_t index, std::string_view field)
		{
			return IndexedKey("restore.review", index, field);
		}

		// The branch is what the tab reviews; without one there is nothing to reopen.
		std::optional<ReviewTabState> ReadReview(const Core::JsonFile& file, std::size_t index)
		{
			ReviewTabState review;
			review.Branch = file.Get(ReviewTabKey(index, "branch"));

			if (review.Branch.empty())
			{
				return std::nullopt;
			}

			if (const std::string base = file.Get(ReviewTabKey(index, "base")); !base.empty())
			{
				review.Base = base;
			}

			const bool hiddenByDefault = false;
			review.IncludeUncommitted = ReadFlag(file.Get(ReviewTabKey(index, "uncommitted")), hiddenByDefault);
			review.Selected = Core::FromUtf8(file.Get(ReviewTabKey(index, "selected")));
			review.SelectedIsUncommitted =
				ReadFlag(file.Get(ReviewTabKey(index, "selectedUncommitted")), hiddenByDefault);

			return review;
		}

		std::size_t ReadCount(const Core::JsonFile& file, std::string_view key)
		{
			return std::min(Core::ParseNumber<std::size_t>(file.Get(key)).value_or(0), kMaximumTabs);
		}

		template<typename Value, std::size_t Size>
		std::string_view NameOf(const std::pair<Value, std::string_view> (&names)[Size], Value value)
		{
			for (const auto& [candidate, name] : names)
			{
				if (candidate == value)
				{
					return name;
				}
			}

			return {};
		}

		template<typename Value, std::size_t Size>
		std::optional<Value> ValueNamed(const std::pair<Value, std::string_view> (&names)[Size],
										std::string_view text)
		{
			for (const auto& [value, name] : names)
			{
				if (name == text)
				{
					return value;
				}
			}

			return std::nullopt;
		}

		std::optional<std::uint8_t> HexDigit(char digit)
		{
			if (digit >= '0' && digit <= '9')
			{
				return static_cast<std::uint8_t>(digit - '0');
			}

			if (digit >= 'a' && digit <= 'f')
			{
				return static_cast<std::uint8_t>(digit - 'a' + 10);
			}

			if (digit >= 'A' && digit <= 'F')
			{
				return static_cast<std::uint8_t>(digit - 'A' + 10);
			}

			return std::nullopt;
		}

		// The whole forty digits or nothing: an abbreviated id could name more than one commit.
		std::optional<Core::Oid> ParseOid(const std::string& text)
		{
			Core::Oid id;

			if (text.size() != id.Bytes.size() * 2)
			{
				return std::nullopt;
			}

			for (std::size_t index = 0; index < id.Bytes.size(); ++index)
			{
				const std::optional<std::uint8_t> high = HexDigit(text[index * 2]);
				const std::optional<std::uint8_t> low = HexDigit(text[index * 2 + 1]);

				if (!high || !low)
				{
					return std::nullopt;
				}

				id.Bytes[index] = static_cast<std::uint8_t>((*high << 4) | *low);
			}

			return id;
		}

		// The targets a working-tree tab can show. Every other one is a commit's, and asks for
		// the commit it is of.
		bool IsWorkingTreeTarget(Core::DiffTarget target)
		{
			return target == Core::DiffTarget::WorkTreeToIndex || target == Core::DiffTarget::IndexToHead ||
				   target == Core::DiffTarget::WorkTreeToHead;
		}

		std::optional<DiffTabState> ReadDiff(const Core::JsonFile& file, std::size_t index)
		{
			const std::string path = file.Get(DiffKey(index, "path"));
			const std::optional<Core::DiffTarget> target =
				ValueNamed(kDiffTargetNames, file.Get(DiffKey(index, "target")));

			if (path.empty() || !target)
			{
				return std::nullopt;
			}

			DiffTabState diff;
			diff.Target = *target;

			// Relative to the repository and never to the folder, as the window holds it: the
			// window is keyed by this path, and imgui.ini remembers its place under that key.
			diff.Path = Core::FromUtf8(path);

			if (const std::string commit = file.Get(DiffKey(index, "commit")); !commit.empty())
			{
				diff.Commit = ParseOid(commit);

				if (!diff.Commit)
				{
					return std::nullopt;
				}
			}

			// A commit's diff without its commit, or the working tree's with one, is no diff the
			// window can show.
			if (diff.Commit.has_value() == IsWorkingTreeTarget(diff.Target))
			{
				return std::nullopt;
			}

			return diff;
		}

		// Relative to the root when it lies there, compared as Core compares every path: a root
		// typed in one case and a tab path drawn from the file tree in another are one folder.
		std::string EncodePath(const std::filesystem::path& path, const std::filesystem::path& root)
		{
			if (const auto relative = Core::RelativeTo(path, root))
			{
				return Core::ToUtf8(*relative);
			}

			return Core::ToUtf8(path);
		}

		std::filesystem::path DecodePath(const std::string& text, const std::filesystem::path& root)
		{
			std::filesystem::path path = Core::FromUtf8(text);

			if (path.is_relative())
			{
				path = root / path;
			}

			path = path.lexically_normal();

			// The root is stored as "." and must read back as the root, without a trailing separator.
			if (path.filename().empty())
			{
				path = path.parent_path();
			}

			return path;
		}
	}

	void RemoveKeysStartingWith(Core::JsonFile& file, std::string_view prefix)
	{
		for (const std::string& key : file.CollectKeys())
		{
			if (key.starts_with(prefix))
			{
				file.Remove(key);
			}
		}
	}

	void WriteGlobalState(const GlobalState& state, Core::JsonFile& file)
	{
		if (state.Window)
		{
			file.Set(kWindowX, std::to_string(state.Window->X));
			file.Set(kWindowY, std::to_string(state.Window->Y));
			file.Set(kWindowWidth, std::to_string(state.Window->Width));
			file.Set(kWindowHeight, std::to_string(state.Window->Height));
			file.Set(kWindowMaximized, Flag(state.Window->Maximized));
		}
		else
		{
			for (const std::string_view key : {kWindowX, kWindowY, kWindowWidth, kWindowHeight, kWindowMaximized})
			{
				file.Remove(key);
			}
		}

		RemoveKeysStartingWith(file, kPanelPrefix);

		for (const auto& [id, visible] : state.PanelVisibility)
		{
			file.Set(std::string{kPanelPrefix} + id + std::string{kPanelSuffix}, Flag(visible));
		}
	}

	GlobalState ReadGlobalState(const Core::JsonFile& file)
	{
		GlobalState state;

		const auto x = Core::ParseNumber<int>(file.Get(kWindowX));
		const auto y = Core::ParseNumber<int>(file.Get(kWindowY));
		const auto width = Core::ParseNumber<int>(file.Get(kWindowWidth));
		const auto height = Core::ParseNumber<int>(file.Get(kWindowHeight));

		if (x && y && width && height && *width > 0 && *height > 0)
		{
			state.Window = Core::WindowPlacement{*x, *y, *width, *height,
												 ReadFlag(file.Get(kWindowMaximized), false)};
		}

		for (const std::string& key : file.CollectKeys())
		{
			if (key.size() <= kPanelPrefix.size() + kPanelSuffix.size() ||
				!key.starts_with(kPanelPrefix) || !key.ends_with(kPanelSuffix))
			{
				continue;
			}

			const std::string value = file.Get(key);

			if (value != "0" && value != "1")
			{
				continue;
			}

			const std::string id = key.substr(
				kPanelPrefix.size(), key.size() - kPanelPrefix.size() - kPanelSuffix.size());
			state.PanelVisibility[id] = value == "1";
		}

		return state;
	}

	void WriteWorkspaceState(const WorkspaceState& state, const std::filesystem::path& root,
							 Core::JsonFile& file)
	{
		RemoveKeysStartingWith(file, kRestorePrefix);

		file.Set(kTabCount, std::to_string(state.Tabs.size()));

		for (std::size_t index = 0; index < state.Tabs.size(); ++index)
		{
			const TabState& tab = state.Tabs[index];

			file.Set(TabKey(index, "path"), EncodePath(tab.Path, root));
			file.Set(TabKey(index, "preview"), Flag(tab.Preview));
			file.Set(TabKey(index, "line"), std::to_string(tab.Line));
			file.Set(TabKey(index, "column"), std::to_string(tab.Column));
			file.Set(TabKey(index, "top"), std::to_string(tab.Top));
		}

		if (state.Active)
		{
			file.Set(kActive, EncodePath(*state.Active, root));
		}

		file.Set(kSessionCount, std::to_string(state.Sessions.size()));

		for (std::size_t index = 0; index < state.Sessions.size(); ++index)
		{
			const SessionTabState& session = state.Sessions[index];

			file.Set(SessionKey(index, "id"), session.Id);
			file.Set(SessionKey(index, "title"), session.Title);
			file.Set(SessionKey(index, "tab"), std::string{NameOf(kSessionTabNames, session.Tab)});
		}

		file.Set(kDiffCount, std::to_string(state.Diffs.size()));

		for (std::size_t index = 0; index < state.Diffs.size(); ++index)
		{
			const DiffTabState& diff = state.Diffs[index];

			file.Set(DiffKey(index, "commit"), diff.Commit ? diff.Commit->ToHex() : std::string{});
			file.Set(DiffKey(index, "target"), std::string{NameOf(kDiffTargetNames, diff.Target)});
			file.Set(DiffKey(index, "path"), Core::ToUtf8(diff.Path));
		}

		file.Set(kReviewCount, std::to_string(state.Reviews.size()));

		for (std::size_t index = 0; index < state.Reviews.size(); ++index)
		{
			const ReviewTabState& review = state.Reviews[index];
			const std::string noBase;

			file.Set(ReviewTabKey(index, "branch"), review.Branch);
			file.Set(ReviewTabKey(index, "base"), review.Base.value_or(noBase));
			file.Set(ReviewTabKey(index, "uncommitted"), Flag(review.IncludeUncommitted));
			file.Set(ReviewTabKey(index, "selected"), Core::ToUtf8(review.Selected));
			file.Set(ReviewTabKey(index, "selectedUncommitted"), Flag(review.SelectedIsUncommitted));
		}

		if (state.Terminals)
		{
			file.Set(kTerminalCount, std::to_string(state.Terminals->size()));

			for (std::size_t index = 0; index < state.Terminals->size(); ++index)
			{
				const TerminalTabState& terminal = (*state.Terminals)[index];

				file.Set(TerminalKey(index, "id"), terminal.Id);
				file.Set(TerminalKey(index, "profile"), terminal.ProfileName);
				file.Set(TerminalKey(index, "directory"), EncodePath(terminal.WorkingDirectory, root));
				file.Set(TerminalKey(index, "title"), terminal.Title);
			}
		}
	}

	WorkspaceState ReadWorkspaceState(const Core::JsonFile& file, const std::filesystem::path& root)
	{
		WorkspaceState state;

		const std::size_t count = ReadCount(file, kTabCount);

		for (std::size_t index = 0; index < count; ++index)
		{
			const std::string path = file.Get(TabKey(index, "path"));

			if (path.empty())
			{
				continue;
			}

			TabState tab;
			tab.Path = DecodePath(path, root);
			tab.Preview = ReadFlag(file.Get(TabKey(index, "preview")), false);
			tab.Line = Core::ParseNumber<std::size_t>(file.Get(TabKey(index, "line"))).value_or(0);
			tab.Column = Core::ParseNumber<std::size_t>(file.Get(TabKey(index, "column"))).value_or(0);
			tab.Top = Core::ParseNumber<std::size_t>(file.Get(TabKey(index, "top"))).value_or(0);

			state.Tabs.push_back(std::move(tab));
		}

		if (const std::string active = file.Get(kActive); !active.empty())
		{
			state.Active = DecodePath(active, root);
		}

		const std::size_t sessions = ReadCount(file, kSessionCount);

		for (std::size_t index = 0; index < sessions; ++index)
		{
			SessionTabState session;
			session.Id = file.Get(SessionKey(index, "id"));

			// The id is what finds the session again; without one there is nothing to look for.
			if (session.Id.empty())
			{
				continue;
			}

			session.Title = file.Get(SessionKey(index, "title"));
			session.Tab = ValueNamed(kSessionTabNames, file.Get(SessionKey(index, "tab")))
							  .value_or(SessionTab::Conversation);

			state.Sessions.push_back(std::move(session));
		}

		const std::size_t diffs = ReadCount(file, kDiffCount);

		for (std::size_t index = 0; index < diffs; ++index)
		{
			if (std::optional<DiffTabState> diff = ReadDiff(file, index))
			{
				state.Diffs.push_back(std::move(*diff));
			}
		}

		const std::size_t reviews = ReadCount(file, kReviewCount);

		for (std::size_t index = 0; index < reviews; ++index)
		{
			if (std::optional<ReviewTabState> review = ReadReview(file, index))
			{
				state.Reviews.push_back(std::move(*review));
			}
		}

		// The count's presence is what tells "never kept" from "kept, and empty".
		if (!file.Get(kTerminalCount).empty())
		{
			std::vector<TerminalTabState> terminals;
			const std::size_t terminalCount = ReadCount(file, kTerminalCount);

			for (std::size_t index = 0; index < terminalCount; ++index)
			{
				TerminalTabState terminal;
				terminal.Id = file.Get(TerminalKey(index, "id"));
				terminal.ProfileName = file.Get(TerminalKey(index, "profile"));
				terminal.Title = file.Get(TerminalKey(index, "title"));

				// Without an id there is no place to go back to, and without a profile no shell
				// to start.
				if (terminal.Id.empty() || terminal.ProfileName.empty())
				{
					continue;
				}

				if (const std::string directory = file.Get(TerminalKey(index, "directory")); !directory.empty())
				{
					terminal.WorkingDirectory = DecodePath(directory, root);
				}

				terminals.push_back(std::move(terminal));
			}

			state.Terminals = std::move(terminals);
		}

		return state;
	}
}
