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

		// Whether the workspace is analysed ahead of being read. See WorkspaceAnalysis for
		// what that costs; the switch exists because the cost is a processor working through
		// a tree nobody asked it to, which on a small machine or an enormous repository is a
		// fair thing to decline.
		static bool LoadBackgroundAnalysis();
		static void SaveBackgroundAnalysis(bool enabled);

		// How much analysis may be kept, in megabytes. Megabytes because that is the unit the
		// panel offers and the only one a person can reason about — the cache itself budgets
		// in bytes, which is the unit it can actually measure an entry in.
		static std::size_t LoadAnalysisBudgetMegabytes();
		static void SaveAnalysisBudgetMegabytes(std::size_t megabytes);

		// How many files the background analysis may have outstanding at once, or zero for the
		// figure derived from the machine. Zero rather than a sentinel like -1 because absent and
		// automatic are the same thing here: a settings file written before this existed, and one
		// written by somebody who ticked the box back on, should read alike.
		static std::size_t LoadAnalysisConcurrency();
		static void SaveAnalysisConcurrency(std::size_t concurrency);

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