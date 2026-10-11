#pragma once

#include "Persistence/PersistedState.h"

#include "Core/Base/JsonFile.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Core {

	class Keymap;

}

namespace Graphite {

	struct LanguageServerSettings
	{
		std::filesystem::path BinaryPath;
		std::vector<std::string> Arguments;
		std::string InitializationOptionsJson;
		std::string SettingsJson;
	};

	// graphite.json, read once and then kept in memory. Getters read the copy in memory; setters
	// change it and mark it unwritten. Nothing re-reads the file per value: Flush reads it once per
	// write and lays only the leaves (a value at a path of keys) this process set or removed over what is on disk, so a leaf
	// edited by hand, or written by another instance, while the application runs survives the
	// write and is read back into memory by it.
	//
	// One instance per process, owned by GraphiteApplication — which needs the window placement
	// before any layer exists — and handed by reference to everything that reads or writes a
	// setting. Not thread-safe: every caller is on the main thread.
	class AppSettings
	{
	public:
		// Reads `file`. A missing file and one that does not parse both read as empty, so every
		// value is its default; neither is written to until a setter changes something.
		explicit AppSettings(std::filesystem::path file);

		AppSettings(AppSettings&&) noexcept = default;
		AppSettings& operator=(AppSettings&&) noexcept = default;

		AppSettings(const AppSettings&) = delete;
		AppSettings& operator=(const AppSettings&) = delete;

		// Writes the file if any setter changed something since the last write: the file as it is
		// on disk now, with this process's changes since then applied over it. A file that exists
		// but does not parse is replaced by the copy in memory instead. A file a newer version of
		// Graphite wrote is never written (see StampSettingsVersion). A write that fails stays
		// unwritten, so the next Flush - the one on exit at the latest - tries again.
		void Flush();

		std::filesystem::path GetLastFolder() const;
		void SetLastFolder(const std::filesystem::path& folder);

		std::string GetGitIgnoredDisplay() const;
		void SetGitIgnoredDisplay(std::string_view mode);

		// Whether the explorer opens its folders to whichever document becomes active.
		bool IsExplorerFollowingActiveFile() const;
		void SetExplorerFollowsActiveFile(bool follow);

		// In percent of the size the system asks for; 100 when nothing usable is stored.
		int GetInterfaceScalePercent() const;
		void SetInterfaceScalePercent(int percent);

		float GetEditorFontSize() const;
		void SetEditorFontSize(float size);

		bool IsIndentingWithSpaces() const;
		void SetIndentWithSpaces(bool useSpaces);

		std::size_t GetIndentWidth() const;
		void SetIndentWidth(std::size_t width);

		bool IsWordWrapEnabled() const;
		void SetWordWrap(bool wrap);

		bool IsDiffWordWrapEnabled() const;
		void SetDiffWordWrap(bool wrap);

		bool HasGutterChangeMarkers() const;
		void SetGutterChangeMarkers(bool show);

		bool HasScrollbarChangeMarkers() const;
		void SetScrollbarChangeMarkers(bool show);

		bool IsHoverInfoEnabled() const;
		void SetHoverInfo(bool show);

		// The definition-preview mode as the word it is stored as; EditorSettings owns what the
		// words mean.
		std::string GetHoverPreview() const;
		void SetHoverPreview(std::string_view mode);

		int GetHoverDelayMs() const;
		void SetHoverDelayMs(int ms);

		bool IsSearchRespectingGitIgnore() const;
		void SetSearchRespectGitIgnore(bool respect);

		bool IsSearchProjectsOnly() const;
		void SetSearchProjectsOnly(bool projectsOnly);

		// Whether the workspace is analysed ahead of being read. See WorkspaceAnalysis for
		// what that costs; the switch exists because the cost is a processor working through
		// a tree nobody asked it to, which on a small machine or an enormous repository is a
		// fair thing to decline.
		bool IsBackgroundAnalysisEnabled() const;
		void SetBackgroundAnalysis(bool enabled);

		// How much analysis may be kept, in megabytes. Megabytes because that is the unit the
		// panel offers and the only one a person can reason about — the cache itself budgets
		// in bytes, which is the unit it can actually measure an entry in.
		std::size_t GetAnalysisBudgetMegabytes() const;
		void SetAnalysisBudgetMegabytes(std::size_t megabytes);

		// How many files the background analysis may have outstanding at once, or zero for the
		// figure derived from the machine. Zero rather than a sentinel like -1 because absent and
		// automatic are the same thing here: a settings file written before this existed, and one
		// written by somebody who ticked the box back on, should read alike.
		std::size_t GetAnalysisConcurrency() const;
		void SetAnalysisConcurrency(std::size_t concurrency);

		std::string GetColorScheme() const;
		void SetColorScheme(std::string_view name);

		std::optional<std::string> GetLanguageServerChoice(std::string_view languageName) const;
		void SetLanguageServerChoice(std::string_view languageName, std::string_view serverId);

		// clangd's path may still sit under the flat key used before servers had settings of their own.
		LanguageServerSettings GetLanguageServerSettings(std::string_view serverId) const;

		// An empty path or list is removed rather than stored, so the server is looked for again.
		void SetLanguageServerBinaryPath(std::string_view serverId, const std::filesystem::path& path);
		void SetLanguageServerArguments(std::string_view serverId, const std::vector<std::string>& arguments);

		// Where claude.exe is, when it is not the one on PATH. Empty means "use PATH".
		std::filesystem::path GetClaudeBinary() const;
		void SetClaudeBinary(const std::filesystem::path& binary);

		// The profile name of the shell a new terminal starts with - the name terminals are saved
		// under. Empty means "the first shell discovered".
		std::string GetDefaultShell() const;
		void SetDefaultShell(std::string_view profileName);

		// Price overrides as model → "input,output,write5m,write1h,read", one key per model
		// under "claude.price.". Read as text; Pricing decides what is a valid price.
		std::vector<std::pair<std::string, std::string>> GetClaudePrices() const;
		void SetClaudePrice(std::string_view model, std::string_view price);
		void RemoveClaudePrice(std::string_view model);

		// The user's own colour schemes (see UserDirectories), whether or not the directory
		// exists yet — the same directory ResolveNamed reads from and the settings panel
		// writes into. Not a value in the file, so static.
		static std::filesystem::path ThemesDirectory();

		// Overrides `keys`' defaults with every binding stored, skipping (and logging) one that
		// does not parse.
		void ApplyKeyBindings(Core::Keymap& keys) const;
		void SetKeyBindings(const Core::Keymap& keys);

		// The window's placement and which panels differ from their default. Written by
		// StateKeeper; read once at startup.
		GlobalState GetGlobalState() const;
		void SetGlobalState(const GlobalState& state);

	private:
		std::string Get(std::string_view key) const;
		void Set(std::string_view key, std::string value);

		// What the file held as of load or the last write, leaf by leaf, so Flush can tell which leaves
		// this process changed since.
		using Snapshot = std::map<std::vector<std::string>, std::string>;
		static Snapshot TakeSnapshot(const Core::JsonFile& document);

		// A leaf that became an object keeps its path in the old snapshot, and removing that path would wipe what was just set beneath it.
		static bool HasLeafUnder(const Snapshot& snapshot, const std::vector<std::string>& prefix);

		std::filesystem::path m_File;
		Core::JsonFile m_Document;
		Snapshot m_Written;
		bool m_Unwritten = false;
		bool m_WriteFailed = false;
	};
}
