#pragma once

#include "Sessions/LocalClock.h"
#include "Sessions/ClaudeLaunch.h"
#include "Sessions/Overview/DashboardColours.h"
#include "Sessions/Overview/DashboardSource.h"
#include "Sessions/Overview/LocalCalendar.h"
#include "Sessions/SessionIndex.h"
#include "Sessions/SessionList.h"
#include "Sessions/SessionSummary.h"
#include "Terminal/CommandTerminal.h"

#include "Core/Base/Base.h"
#include "Core/FileSystem/DirectoryWatcher.h"
#include "Core/UI/Panel.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	class ActiveScheme;
	class ClaudeSettings;
	class SessionWindows;
	class Workspace;

	enum class OverviewContent : std::uint8_t;

	enum class SessionsTab : std::uint8_t
	{
		Overview,
		Sessions
	};

	struct SessionsPanelChoice
	{
		SessionsTab Tab = SessionsTab::Overview;
		bool WorkspaceOnly = true;

		friend bool operator==(const SessionsPanelChoice&, const SessionsPanelChoice&) = default;
	};

	class SessionsPanel final : public Core::Panel
	{
	public:
		// WARNING: Claude Code looks up a transcript to resume under the project of the directory it is started
		// in, so a resume started from the wrong directory fails to find the session. An empty result means
		// Claude did not start.
		using LaunchClaude = std::function<Core::Ref<CommandTerminal>(
			const std::string& label, const ClaudeCommand& command, const std::filesystem::path& workingDirectory)>;

		SessionsPanel(const Workspace& workspace, const ClaudeSettings& settings, const ActiveScheme& scheme,
					  SessionWindows& windows, LaunchClaude launch);
		~SessionsPanel() override;

		SessionsPanel(const SessionsPanel&) = delete;
		SessionsPanel& operator=(const SessionsPanel&) = delete;

		void OnUpdate(Core::Timestep timestep) override;
		void OnImGuiRender() override;

		SessionsPanelChoice GetChoice() const;

		void SetChoice(const SessionsPanelChoice& choice);

	private:
		void FindProjectsDirectory();
		void StartIndexingWhenPresent();
		void StartIndexing();
		void TakeFromIndex();
		void TakeFromWatcher();
		void SaveCacheWhenSettled();
		void SaveCache();

		const LocalCalendar& Calendar();
		std::filesystem::path ScopeFolder() const;
		void RefreshOverview();

		void RenderToolbar(bool viewIsLoading);
		void RenderToolbarEnd(bool viewIsLoading);
		OverviewContent OverviewShows() const;
		void RenderOverview();
		std::vector<SessionListRow> ListedRows();
		void RenderList(const std::vector<SessionListRow>& rows);
		void RenderDayFilter(std::size_t listed);
		DashboardColours Colours() const;

		const SessionSummary* FindSessionById(const std::string& id) const;

		void StartNewSession();
		void ContinueSession(const SessionSummary& summary);

		std::filesystem::path WorkspaceRootOrCurrentDirectory() const;

		const Workspace& m_Workspace;
		const ClaudeSettings& m_Settings;
		const ActiveScheme& m_Scheme;
		SessionWindows& m_Windows;
		LaunchClaude m_Launch;

		std::filesystem::path m_Projects;
		std::filesystem::path m_CacheFile;

		std::optional<std::string> m_Problem;
		std::optional<std::string> m_LaunchProblem;

		std::chrono::steady_clock::time_point m_NextDirectoryCheck;

		std::map<std::filesystem::path, SessionSummary> m_Sessions;

		LocalClock m_Clock;
		std::optional<LocalCalendar> m_Calendar;

		// WARNING: Points into m_Sessions, so every change to them is marked on it before the next draw reads it.
		DashboardSource m_Overview;

		SessionsTab m_Tab = SessionsTab::Overview;
		bool m_WorkspaceOnly = true;
		std::string m_Filter;
		std::optional<std::chrono::year_month_day> m_Day;
		bool m_Started = false;
		bool m_Indexing = false;

		bool m_InitialScan = false;
		bool m_CacheGaveNothing = true;
		ScanProgress m_ScanProgress;

		bool m_CacheDirty = false;
		std::chrono::steady_clock::time_point m_LastCacheSave;

		Core::Scope<Core::DirectoryWatcher> m_Watcher;

		// WARNING: Keep last. Its worker is joined before anything above goes away.
		SessionIndex m_Index;
	};
}
