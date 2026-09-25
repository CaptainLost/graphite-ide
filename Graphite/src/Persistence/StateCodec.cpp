#include "Persistence/StateCodec.h"

#include "Core/Base/JsonFile.h"
#include "Core/Fs/FileSystem.h"

#include <algorithm>
#include <charconv>
#include <cwctype>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

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

		// More tabs than anybody keeps open, and a ceiling on what a damaged count can cost.
		constexpr std::size_t kMaximumTabs = 256;

		template <typename Number>
		std::optional<Number> ParseNumber(const std::string& text)
		{
			if (text.empty())
			{
				return std::nullopt;
			}

			Number value{};
			const char* last = text.data() + text.size();
			const auto [end, error] = std::from_chars(text.data(), last, value);

			if (error != std::errc{} || end != last)
			{
				return std::nullopt;
			}

			return value;
		}

		std::string Flag(bool value)
		{
			return value ? "1" : "0";
		}

		std::string TabKey(std::size_t index, std::string_view field)
		{
			return "restore.tab." + std::to_string(index) + "." + std::string{field};
		}

		void RemoveKeysStartingWith(Core::JsonFile& file, std::string_view prefix)
		{
			for (const std::string& key : file.GetKeys())
			{
				if (key.starts_with(prefix))
				{
					file.Remove(key);
				}
			}
		}

		// Windows paths are case-insensitive, but std::filesystem compares path elements
		// case-sensitively. A root typed in one case and a tab path drawn from the file tree in
		// another must still be recognised as the same folder.
		bool EqualElementIgnoringCase(const std::filesystem::path& a, const std::filesystem::path& b)
		{
			const auto& left = a.native();
			const auto& right = b.native();

			if (left.size() != right.size())
			{
				return false;
			}

			for (std::size_t index = 0; index < left.size(); ++index)
			{
				if (std::towlower(static_cast<std::wint_t>(left[index])) !=
					std::towlower(static_cast<std::wint_t>(right[index])))
				{
					return false;
				}
			}

			return true;
		}

		std::string EncodePath(const std::filesystem::path& path, const std::filesystem::path& root)
		{
			if (!root.empty())
			{
				const std::filesystem::path normalizedPath = path.lexically_normal();
				const std::filesystem::path normalizedRoot = root.lexically_normal();

				auto rootIt = normalizedRoot.begin();
				auto pathIt = normalizedPath.begin();
				bool inside = true;

				for (; rootIt != normalizedRoot.end(); ++rootIt, ++pathIt)
				{
					if (pathIt == normalizedPath.end() || !EqualElementIgnoringCase(*rootIt, *pathIt))
					{
						inside = false;
						break;
					}
				}

				if (inside)
				{
					std::filesystem::path relative;

					if (pathIt == normalizedPath.end())
					{
						// path is the root itself.
						relative = ".";
					}
					else
					{
						for (; pathIt != normalizedPath.end(); ++pathIt)
						{
							relative /= *pathIt;
						}
					}

					return Core::ToUtf8(relative);
				}
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

			return path.lexically_normal();
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

		const auto x = ParseNumber<int>(file.Get(kWindowX));
		const auto y = ParseNumber<int>(file.Get(kWindowY));
		const auto width = ParseNumber<int>(file.Get(kWindowWidth));
		const auto height = ParseNumber<int>(file.Get(kWindowHeight));

		if (x && y && width && height && *width > 0 && *height > 0)
		{
			state.Window = Core::WindowPlacement{*x, *y, *width, *height,
												 file.Get(kWindowMaximized) == "1"};
		}

		for (const std::string& key : file.GetKeys())
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
	}

	WorkspaceState ReadWorkspaceState(const Core::JsonFile& file, const std::filesystem::path& root)
	{
		WorkspaceState state;

		const std::size_t count =
			std::min(ParseNumber<std::size_t>(file.Get(kTabCount)).value_or(0), kMaximumTabs);

		for (std::size_t index = 0; index < count; ++index)
		{
			const std::string path = file.Get(TabKey(index, "path"));

			if (path.empty())
			{
				continue;
			}

			TabState tab;
			tab.Path = DecodePath(path, root);
			tab.Preview = file.Get(TabKey(index, "preview")) == "1";
			tab.Line = ParseNumber<std::size_t>(file.Get(TabKey(index, "line"))).value_or(0);
			tab.Column = ParseNumber<std::size_t>(file.Get(TabKey(index, "column"))).value_or(0);
			tab.Top = ParseNumber<std::size_t>(file.Get(TabKey(index, "top"))).value_or(0);

			state.Tabs.push_back(std::move(tab));
		}

		if (const std::string active = file.Get(kActive); !active.empty())
		{
			state.Active = DecodePath(active, root);
		}

		return state;
	}
}
