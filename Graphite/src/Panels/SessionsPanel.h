#pragma once

#include "Panels/LocalClock.h"
#include "Sessions/ClaudeLaunch.h"
#include "Sessions/SessionIndex.h"

#include "Core/Base/Base.h"
#include "Core/FileSystem/DirectoryWatcher.h"
#include "Core/UI/Panel.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	class ClaudeSettings;
	class SessionWindows;
	class Workspace;

	// Every Claude session on this machine, newest first, with what each cost. Docked in the
	// centre beside settings and git: it is somewhere a reader goes to look, not a strip kept
	// open beside the code.
	class SessionsPanel final : public Core::Panel
	{
	public:
		// Opens `command` in a terminal tab labelled `label`, started in `workingDirectory`;
		// false when it could not start. The directory matters here: Claude Code looks up a
		// transcript to resume under the project of the directory it is started in, so resuming
		// a session started elsewhere from the wrong directory fails to find it.
		using LaunchClaude = std::function<bool(const std::string& label, const ClaudeCommand& command,
												const std::filesystem::path& workingDirectory)>;

		SessionsPanel(const Workspace& workspace, const ClaudeSettings& settings, SessionWindows& windows,
					  LaunchClaude launch);
		~SessionsPanel() override;

		SessionsPanel(const SessionsPanel&) = delete;
		SessionsPanel& operator=(const SessionsPanel&) = delete;

		void OnUpdate(Core::Timestep ts) override;
		void OnImGuiRender() override;

	private:
		// A session as the list draws it: the folder key it is grouped and sorted by, worked
		// out once per frame rather than once per comparison.
		struct VisibleRow
		{
			const SessionSummary* Summary = nullptr;
			std::filesystem::path::string_type Folder;
		};

		void FindProjectsDirectory();
		void StartIndexingWhenPresent();
		void StartIndexing();
		void TakeFromIndex();
		void TakeFromWatcher();
		void SaveCacheWhenSettled();
		void SaveCache();

		void RenderToolbar();
		void RenderList();
		void RenderRow(const SessionSummary& summary);

		void StartNewSession();
		void ContinueSession(const SessionSummary& summary);

		std::vector<VisibleRow> VisibleSessions() const;

		// The workspace root, or the process's current directory when none is open: where a
		// launch starts unless the summary it resumes names somewhere more specific.
		std::filesystem::path WorkspaceRoot() const;

		const Workspace& m_Workspace;
		const ClaudeSettings& m_Settings;
		SessionWindows& m_Windows;
		LaunchClaude m_Launch;

		std::filesystem::path m_Projects;
		std::filesystem::path m_CacheFile;

		// Why there is nothing to show, when there is nothing. With m_Projects empty it is final
		// (no home directory); otherwise the directory is missing and may yet appear, which the
		// first Claude session started on this machine does.
		std::optional<std::string> m_Problem;
		std::optional<std::string> m_LaunchProblem;

		// When the missing projects directory is looked for again.
		std::chrono::steady_clock::time_point m_NextDirectoryCheck;

		std::map<std::filesystem::path, SessionSummary> m_Sessions;

		// Each row's date on the reader's clock, as the session views write their times: a
		// session that ended late in the evening belongs to that evening's day, not UTC's.
		LocalClock m_Clock;

		bool m_WorkspaceOnly = true;
		std::string m_Filter;
		bool m_Started = false;
		bool m_Indexing = false;

		// True from Start until the index first goes idle. Refreshes run the index too, while a
		// live session writes, and a notice that came and went with each would flicker.
		bool m_InitialScan = false;

		// Written at most every kCacheSaveInterval while dirty, and once more on the way out:
		// a live session delivers a summary with every turn, and each save rewrites every one.
		bool m_CacheDirty = false;
		std::chrono::steady_clock::time_point m_LastCacheSave;

		Core::Scope<Core::DirectoryWatcher> m_Watcher;

		// Last, deliberately: its worker is joined before anything above goes away.
		SessionIndex m_Index;
	};
}
