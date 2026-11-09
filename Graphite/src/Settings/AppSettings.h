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

	class AppSettings
	{
	public:
		explicit AppSettings(std::filesystem::path file);

		AppSettings(AppSettings&&) noexcept = default;
		AppSettings& operator=(AppSettings&&) noexcept = default;

		AppSettings(const AppSettings&) = delete;
		AppSettings& operator=(const AppSettings&) = delete;

		void Flush();

		std::filesystem::path GetLastFolder() const;
		void SetLastFolder(const std::filesystem::path& folder);

		// The folders opened last, the newest first, at most kRecentFolderCount, each once whatever its case.
		static constexpr std::size_t kRecentFolderCount = 10;
		std::vector<std::filesystem::path> GetRecentFolders() const;
		void NoteRecentFolder(const std::filesystem::path& folder);
		void ForgetRecentFolder(const std::filesystem::path& folder);

		std::string GetGitIgnoredDisplay() const;
		void SetGitIgnoredDisplay(std::string_view mode);

		bool IsExplorerFollowingActiveFile() const;
		void SetExplorerFollowsActiveFile(bool follow);

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

		bool IsSaveOnFocusLossEnabled() const;
		void SetSaveOnFocusLoss(bool save);

		bool IsDiffWordWrapEnabled() const;
		void SetDiffWordWrap(bool wrap);

		std::string GetDiffLayout() const;
		void SetDiffLayout(std::string_view layout);

		std::string GetImageBackground() const;
		void SetImageBackground(std::string_view background);

		bool HasImagePixelGrid() const;
		void SetImagePixelGrid(bool show);

		std::string GetDiffScope() const;
		void SetDiffScope(std::string_view scope);

		bool IsDiffSyntaxColoured() const;
		void SetDiffSyntaxColoured(bool colour);

		bool HasGutterChangeMarkers() const;
		void SetGutterChangeMarkers(bool show);

		bool HasScrollbarChangeMarkers() const;
		void SetScrollbarChangeMarkers(bool show);

		bool IsHighlightingOccurrences() const;
		void SetHighlightOccurrences(bool highlight);

		bool HasLineAuthor() const;
		void SetLineAuthor(bool show);

		int GetLineAuthorDelayMs() const;
		void SetLineAuthorDelayMs(int milliseconds);

		bool IsHoverInfoEnabled() const;
		void SetHoverInfo(bool show);

		std::string GetHoverPreview() const;
		void SetHoverPreview(std::string_view mode);

		int GetHoverDelayMs() const;
		void SetHoverDelayMs(int milliseconds);

		bool IsSearchRespectingGitIgnore() const;
		void SetSearchRespectGitIgnore(bool respect);

		bool IsSearchProjectsOnly() const;
		void SetSearchProjectsOnly(bool projectsOnly);

		bool IsBackgroundAnalysisEnabled() const;
		void SetBackgroundAnalysis(bool enabled);

		std::size_t GetAnalysisBudgetMegabytes() const;
		void SetAnalysisBudgetMegabytes(std::size_t megabytes);

		std::size_t GetAnalysisConcurrency() const;
		void SetAnalysisConcurrency(std::size_t concurrency);

		std::string GetColorScheme() const;
		void SetColorScheme(std::string_view name);

		std::optional<std::string> GetLanguageServerChoice(std::string_view languageName) const;
		void SetLanguageServerChoice(std::string_view languageName, std::string_view serverId);

		LanguageServerSettings GetLanguageServerSettings(std::string_view serverId) const;

		void SetLanguageServerBinaryPath(std::string_view serverId, const std::filesystem::path& path);
		void SetLanguageServerArguments(std::string_view serverId, const std::vector<std::string>& arguments);

		std::filesystem::path GetClaudeBinary() const;
		void SetClaudeBinary(const std::filesystem::path& binary);

		std::string GetDefaultShell() const;
		void SetDefaultShell(std::string_view profileName);

		std::vector<std::pair<std::string, std::string>> GetClaudePrices() const;
		void SetClaudePrice(std::string_view model, std::string_view price);
		void RemoveClaudePrice(std::string_view model);

		static std::filesystem::path ThemesDirectory();

		void ApplyKeyBindings(Core::Keymap& keys) const;
		void SetKeyBindings(const Core::Keymap& keys);

		GlobalState GetGlobalState() const;
		void SetGlobalState(const GlobalState& state);

	private:
		std::string Get(std::string_view key) const;
		void Set(std::string_view key, std::string value);

		void MoveOldServerPath();

		using Snapshot = std::map<std::vector<std::string>, std::string>;
		static Snapshot TakeSnapshot(const Core::JsonFile& document);

		static bool HasLeafUnder(const Snapshot& snapshot, const std::vector<std::string>& prefix);

		std::filesystem::path m_File;
		Core::JsonFile m_Document;
		Snapshot m_Written;
		bool m_Unwritten = false;
		bool m_WriteFailed = false;
	};
}
