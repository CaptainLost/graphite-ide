#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace Core {

	class Keymap;

}

namespace Graphite {

	class AppSettings
	{
	public:
		static std::filesystem::path LoadLastFolder();
		static void SaveLastFolder(const std::filesystem::path& folder);

		static std::string LoadGitIgnoredDisplay();
		static void SaveGitIgnoredDisplay(std::string_view mode);

		static float LoadEditorFontSize();
		static void SaveEditorFontSize(float size);

		static bool LoadIndentWithSpaces();
		static void SaveIndentWithSpaces(bool useSpaces);

		static std::size_t LoadIndentWidth();
		static void SaveIndentWidth(std::size_t width);

		static bool LoadWordWrap();
		static void SaveWordWrap(bool wrap);

		static std::string LoadColorScheme();
		static void SaveColorScheme(std::string_view name);

		// The executable directory's themes subdirectory, whether or not it exists yet — the
		// same directory ResolveNamed reads from and the settings panel writes into.
		static std::filesystem::path ThemesDirectory();

		static void LoadKeyBindings(Core::Keymap& keys);

		static void SaveKeyBindings(const Core::Keymap& keys);
	};
}