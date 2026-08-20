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

		static bool LoadSearchRespectGitIgnore();
		static void SaveSearchRespectGitIgnore(bool respect);

		static bool LoadSearchProjectsOnly();
		static void SaveSearchProjectsOnly(bool projectsOnly);

		static std::string LoadColorScheme();
		static void SaveColorScheme(std::string_view name);

		// Where to find the C++ language server, for a user who wants neither the copy beside the
		// application nor whichever one is on PATH. Empty — the default — means "search as usual".
		// This is the "configured location" the status bar's "not found" message names, and the
		// only thing that makes that sentence true.
		//
		// `languageServerBinary` is hand-edited in `graphite.json` today: a settings-panel widget
		// for it is deliberately not part of this pass, since the message promises a configured
		// location rather than a particular way of configuring it. `SaveLanguageServerBinary` is
		// therefore uncalled by design, kept as the writing half of the pair so that a widget, or
		// a "locate clangd" prompt, has somewhere to write without reopening the JSON key.
		static std::filesystem::path LoadLanguageServerBinary();
		static void SaveLanguageServerBinary(const std::filesystem::path& binary);

		// The executable directory's themes subdirectory, whether or not it exists yet — the
		// same directory ResolveNamed reads from and the settings panel writes into.
		static std::filesystem::path ThemesDirectory();

		static void LoadKeyBindings(Core::Keymap& keys);

		static void SaveKeyBindings(const Core::Keymap& keys);
	};
}