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
#include <vector>

namespace Graphite {

	namespace {
		constexpr std::string_view kWindowX = "window.x";
		constexpr std::string_view kWindowY = "window.y";
		constexpr std::string_view kWindowWidth = "window.width";
		constexpr std::string_view kWindowHeight = "window.height";
		constexpr std::string_view kWindowMaximized = "window.maximized";

		constexpr std::string_view kTerminalsHidden = "terminals.hidden";

		constexpr std::string_view kPanelPrefix = "panel.";
		constexpr std::string_view kPanelSuffix = ".visible";

		constexpr std::string_view kRestorePrefix = "restore.";
		constexpr std::string_view kTabCount = "restore.tab.count";
		constexpr std::string_view kActivePath = "restore.active";

		constexpr std::string_view kSessionCount = "restore.session.count";
		constexpr std::string_view kDiffCount = "restore.diff.count";
		constexpr std::string_view kTerminalCount = "restore.terminal.count";
		constexpr std::string_view kReviewCount = "restore.review.count";

		constexpr std::size_t kMaximumListEntries = 256;
		constexpr std::size_t kNumberWhenUnreadable = 0;

		// WARNING: The words are written out rather than numbered, so reordering an enum in a
		// later version cannot quietly turn one saved value into another.
		constexpr std::pair<SessionTab, std::string_view> kSessionTabNames[] = {
			{SessionTab::Conversation, "conversation"},
			{SessionTab::Cost, "cost"},
			{SessionTab::Requests, "requests"},
		};

		constexpr std::string_view kBranchListSort = "restore.branchList.sort";
		constexpr std::string_view kBranchListGroup = "restore.branchList.group";

		constexpr std::pair<BranchSort, std::string_view> kBranchSortNames[] = {
			{BranchSort::Newest, "newest"},
			{BranchSort::Oldest, "oldest"},
			{BranchSort::Name, "name"},
		};

		constexpr std::pair<BranchGrouping, std::string_view> kBranchGroupingNames[] = {
			{BranchGrouping::None, "none"},
			{BranchGrouping::Author, "author"},
		};

		constexpr std::pair<Core::DiffTarget, std::string_view> kDiffTargetNames[] = {
			{Core::DiffTarget::WorkTreeToIndex, "unstaged"},
			{Core::DiffTarget::IndexToHead, "staged"},
			{Core::DiffTarget::WorkTreeToHead, "working"},
			{Core::DiffTarget::CommitToParent, "commit"},
			{Core::DiffTarget::RevisionToRevision, "range"},
		};

		std::string IndexedKey(std::string_view prefix, std::size_t entryIndex, std::string_view field)
		{
			return std::string{prefix} + "." + std::to_string(entryIndex) + "." + std::string{field};
		}

		std::string TabKey(std::size_t entryIndex, std::string_view field)
		{
			return IndexedKey("restore.tab", entryIndex, field);
		}

		std::string SessionKey(std::size_t entryIndex, std::string_view field)
		{
			return IndexedKey("restore.session", entryIndex, field);
		}

		std::string DiffKey(std::size_t entryIndex, std::string_view field)
		{
			return IndexedKey("restore.diff", entryIndex, field);
		}

		std::string TerminalKey(std::size_t entryIndex, std::string_view field)
		{
			return IndexedKey("restore.terminal", entryIndex, field);
		}

		std::string ReviewTabKey(std::size_t entryIndex, std::string_view field)
		{
			return IndexedKey("restore.review", entryIndex, field);
		}

		std::optional<ReviewTabState> ReadReview(const Core::JsonFile& file, std::size_t entryIndex)
		{
			ReviewTabState review;
			review.Branch = file.Get(ReviewTabKey(entryIndex, "branch"));

			if (review.Branch.empty())
			{
				return std::nullopt;
			}

			const bool committedByDefault = false;
			review.Selected = Core::FromUtf8(file.Get(ReviewTabKey(entryIndex, "selected")));
			review.SelectedIsUncommitted =
				ReadFlag(file.Get(ReviewTabKey(entryIndex, "selectedUncommitted")), committedByDefault);

			return review;
		}

		std::size_t ReadCount(const Core::JsonFile& file, std::string_view key)
		{
			const std::size_t storedCount =
				Core::ParseNumber<std::size_t>(file.Get(key)).value_or(kNumberWhenUnreadable);

			return std::min(storedCount, kMaximumListEntries);
		}

		std::size_t ReadPosition(const Core::JsonFile& file, std::size_t tabIndex, std::string_view field)
		{
			return Core::ParseNumber<std::size_t>(file.Get(TabKey(tabIndex, field)))
				.value_or(kNumberWhenUnreadable);
		}

		template<typename Value, std::size_t Size>
		std::string_view NameFromTable(const std::pair<Value, std::string_view> (&names)[Size], Value value)
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

		bool IsWorkingTreeTarget(Core::DiffTarget target)
		{
			return target == Core::DiffTarget::WorkTreeToIndex || target == Core::DiffTarget::IndexToHead ||
				   target == Core::DiffTarget::WorkTreeToHead;
		}

		std::optional<DiffTabState> ReadDiff(const Core::JsonFile& file, std::size_t entryIndex)
		{
			const std::string path = file.Get(DiffKey(entryIndex, "path"));
			const std::optional<Core::DiffTarget> target =
				ValueNamed(kDiffTargetNames, file.Get(DiffKey(entryIndex, "target")));

			if (path.empty() || !target)
			{
				return std::nullopt;
			}

			DiffTabState diff;
			diff.Target = *target;

			// WARNING: The path stays relative to the repository and is never joined to the folder:
			// the window is keyed by this path, and imgui.ini remembers its place under that key.
			diff.Path = Core::FromUtf8(path);

			if (const std::string commit = file.Get(DiffKey(entryIndex, "commit")); !commit.empty())
			{
				diff.Commit = Core::Oid::FromHex(commit);

				if (!diff.Commit)
				{
					return std::nullopt;
				}
			}

			const bool commitContradictsTarget = diff.Commit.has_value() == IsWorkingTreeTarget(diff.Target);

			if (commitContradictsTarget)
			{
				return std::nullopt;
			}

			return diff;
		}

		std::string EncodePath(const std::filesystem::path& path, const std::filesystem::path& root)
		{
			if (const std::optional<std::filesystem::path> relative = Core::RelativeTo(path, root))
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

			const bool endsWithSeparator = path.filename().empty();

			if (endsWithSeparator)
			{
				path = path.parent_path();
			}

			return path;
		}

		bool IsPanelKey(const std::string& key)
		{
			const bool longEnoughForAnId = key.size() > kPanelPrefix.size() + kPanelSuffix.size();

			return longEnoughForAnId && key.starts_with(kPanelPrefix) && key.ends_with(kPanelSuffix);
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

		for (const auto& [panelId, visible] : state.PanelVisibility)
		{
			const std::string panelKey = std::string{kPanelPrefix} + panelId + std::string{kPanelSuffix};

			file.Set(panelKey, Flag(visible));
		}

		if (state.TerminalsHidden)
		{
			file.Set(kTerminalsHidden, Flag(true));
		}
		else
		{
			file.Remove(kTerminalsHidden);
		}
	}

	GlobalState ReadGlobalState(const Core::JsonFile& file)
	{
		GlobalState state;

		const std::optional<int> windowX = Core::ParseNumber<int>(file.Get(kWindowX));
		const std::optional<int> windowY = Core::ParseNumber<int>(file.Get(kWindowY));
		const std::optional<int> windowWidth = Core::ParseNumber<int>(file.Get(kWindowWidth));
		const std::optional<int> windowHeight = Core::ParseNumber<int>(file.Get(kWindowHeight));

		if (windowX && windowY && windowWidth && windowHeight && *windowWidth > 0 && *windowHeight > 0)
		{
			const bool restoredByDefault = false;

			state.Window = Core::WindowPlacement{
				*windowX, *windowY, *windowWidth, *windowHeight,
				ReadFlag(file.Get(kWindowMaximized), restoredByDefault)};
		}

		for (const std::string& key : file.CollectKeys())
		{
			if (!IsPanelKey(key))
			{
				continue;
			}

			const std::string value = file.Get(key);

			if (value != kStoredFalse && value != kStoredTrue)
			{
				continue;
			}

			const std::size_t panelIdLength = key.size() - kPanelPrefix.size() - kPanelSuffix.size();
			const std::string panelId = key.substr(kPanelPrefix.size(), panelIdLength);
			state.PanelVisibility[panelId] = value == kStoredTrue;
		}

		const bool shownByDefault = false;
		state.TerminalsHidden = ReadFlag(file.Get(kTerminalsHidden), shownByDefault);

		return state;
	}

	void WriteWorkspaceState(const WorkspaceState& state, const std::filesystem::path& root,
							 Core::JsonFile& file)
	{
		RemoveKeysStartingWith(file, kRestorePrefix);

		file.Set(kTabCount, std::to_string(state.Tabs.size()));

		for (std::size_t tabIndex = 0; tabIndex < state.Tabs.size(); ++tabIndex)
		{
			const TabState& tab = state.Tabs[tabIndex];

			file.Set(TabKey(tabIndex, "path"), EncodePath(tab.Path, root));
			file.Set(TabKey(tabIndex, "preview"), Flag(tab.Preview));
			file.Set(TabKey(tabIndex, "line"), std::to_string(tab.Line));
			file.Set(TabKey(tabIndex, "column"), std::to_string(tab.Column));
			file.Set(TabKey(tabIndex, "top"), std::to_string(tab.Top));
		}

		if (state.Active)
		{
			file.Set(kActivePath, EncodePath(*state.Active, root));
		}

		file.Set(kSessionCount, std::to_string(state.Sessions.size()));

		for (std::size_t sessionIndex = 0; sessionIndex < state.Sessions.size(); ++sessionIndex)
		{
			const SessionTabState& session = state.Sessions[sessionIndex];

			file.Set(SessionKey(sessionIndex, "id"), session.Id);
			file.Set(SessionKey(sessionIndex, "title"), session.Title);
			file.Set(SessionKey(sessionIndex, "tab"), std::string{NameFromTable(kSessionTabNames, session.Tab)});
		}

		file.Set(kDiffCount, std::to_string(state.Diffs.size()));

		for (std::size_t diffIndex = 0; diffIndex < state.Diffs.size(); ++diffIndex)
		{
			const DiffTabState& diff = state.Diffs[diffIndex];
			const std::string commitHex = diff.Commit ? diff.Commit->ToHex() : std::string{};

			file.Set(DiffKey(diffIndex, "commit"), commitHex);
			file.Set(DiffKey(diffIndex, "target"), std::string{NameFromTable(kDiffTargetNames, diff.Target)});
			file.Set(DiffKey(diffIndex, "path"), Core::ToUtf8(diff.Path));
		}

		file.Set(kReviewCount, std::to_string(state.Reviews.size()));

		for (std::size_t reviewIndex = 0; reviewIndex < state.Reviews.size(); ++reviewIndex)
		{
			const ReviewTabState& review = state.Reviews[reviewIndex];

			file.Set(ReviewTabKey(reviewIndex, "branch"), review.Branch);
			file.Set(ReviewTabKey(reviewIndex, "selected"), Core::ToUtf8(review.Selected));
			file.Set(ReviewTabKey(reviewIndex, "selectedUncommitted"), Flag(review.SelectedIsUncommitted));
		}

		if (state.Terminals)
		{
			file.Set(kTerminalCount, std::to_string(state.Terminals->size()));

			for (std::size_t terminalIndex = 0; terminalIndex < state.Terminals->size(); ++terminalIndex)
			{
				const TerminalTabState& terminal = (*state.Terminals)[terminalIndex];

				file.Set(TerminalKey(terminalIndex, "id"), terminal.Id);
				file.Set(TerminalKey(terminalIndex, "profile"), terminal.ProfileName);
				file.Set(TerminalKey(terminalIndex, "directory"), EncodePath(terminal.WorkingDirectory, root));
				file.Set(TerminalKey(terminalIndex, "title"), terminal.Title);
			}
		}

		file.Set(kBranchListSort, std::string{NameFromTable(kBranchSortNames, state.BranchList.Sort)});
		file.Set(kBranchListGroup, std::string{NameFromTable(kBranchGroupingNames, state.BranchList.Group)});
	}

	WorkspaceState ReadWorkspaceState(const Core::JsonFile& file, const std::filesystem::path& root)
	{
		WorkspaceState state;

		const std::size_t tabCount = ReadCount(file, kTabCount);

		for (std::size_t tabIndex = 0; tabIndex < tabCount; ++tabIndex)
		{
			const std::string path = file.Get(TabKey(tabIndex, "path"));

			if (path.empty())
			{
				continue;
			}

			const bool openedForGoodByDefault = false;

			TabState tab;
			tab.Path = DecodePath(path, root);
			tab.Preview = ReadFlag(file.Get(TabKey(tabIndex, "preview")), openedForGoodByDefault);
			tab.Line = ReadPosition(file, tabIndex, "line");
			tab.Column = ReadPosition(file, tabIndex, "column");
			tab.Top = ReadPosition(file, tabIndex, "top");

			state.Tabs.push_back(std::move(tab));
		}

		if (const std::string active = file.Get(kActivePath); !active.empty())
		{
			state.Active = DecodePath(active, root);
		}

		const std::size_t sessionCount = ReadCount(file, kSessionCount);

		for (std::size_t sessionIndex = 0; sessionIndex < sessionCount; ++sessionIndex)
		{
			SessionTabState session;
			session.Id = file.Get(SessionKey(sessionIndex, "id"));

			if (session.Id.empty())
			{
				continue;
			}

			session.Title = file.Get(SessionKey(sessionIndex, "title"));
			session.Tab = ValueNamed(kSessionTabNames, file.Get(SessionKey(sessionIndex, "tab")))
							  .value_or(SessionTab::Conversation);

			state.Sessions.push_back(std::move(session));
		}

		const std::size_t diffCount = ReadCount(file, kDiffCount);

		for (std::size_t diffIndex = 0; diffIndex < diffCount; ++diffIndex)
		{
			if (std::optional<DiffTabState> diff = ReadDiff(file, diffIndex))
			{
				state.Diffs.push_back(std::move(*diff));
			}
		}

		const std::size_t reviewCount = ReadCount(file, kReviewCount);

		for (std::size_t reviewIndex = 0; reviewIndex < reviewCount; ++reviewIndex)
		{
			if (std::optional<ReviewTabState> review = ReadReview(file, reviewIndex))
			{
				state.Reviews.push_back(std::move(*review));
			}
		}

		const bool terminalsWereKept = !file.Get(kTerminalCount).empty();

		if (terminalsWereKept)
		{
			std::vector<TerminalTabState> terminals;
			const std::size_t terminalCount = ReadCount(file, kTerminalCount);

			for (std::size_t terminalIndex = 0; terminalIndex < terminalCount; ++terminalIndex)
			{
				TerminalTabState terminal;
				terminal.Id = file.Get(TerminalKey(terminalIndex, "id"));
				terminal.ProfileName = file.Get(TerminalKey(terminalIndex, "profile"));
				terminal.Title = file.Get(TerminalKey(terminalIndex, "title"));

				if (terminal.Id.empty() || terminal.ProfileName.empty())
				{
					continue;
				}

				if (const std::string directory = file.Get(TerminalKey(terminalIndex, "directory"));
					!directory.empty())
				{
					terminal.WorkingDirectory = DecodePath(directory, root);
				}

				terminals.push_back(std::move(terminal));
			}

			state.Terminals = std::move(terminals);
		}

		const BranchListState branchListByDefault;
		state.BranchList.Sort = ValueNamed(kBranchSortNames, file.Get(kBranchListSort)).value_or(branchListByDefault.Sort);
		state.BranchList.Group =
			ValueNamed(kBranchGroupingNames, file.Get(kBranchListGroup)).value_or(branchListByDefault.Group);

		return state;
	}
}
