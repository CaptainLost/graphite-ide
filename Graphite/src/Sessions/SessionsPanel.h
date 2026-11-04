#pragma once

#include "Sessions/LocalClock.h"
#include "Sessions/ClaudeLaunch.h"
#include "Sessions/SessionIndex.h"
#include "Sessions/SessionSummary.h"
#include "Terminal/CommandTerminal.h"

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

	struct SessionRowText
	{
		std::string Title;
		std::string Detail;
		std::string DetailHint;
	};

	enum class SessionRowAction
	{
		None,
		Open,
		Continue
	};

	SessionRowAction DrawSessionRow(const SessionRowText& text);

	class SessionsPanel final : public Core::Panel
	{
	public:
		// WARNING: Claude Code looks up a transcript to resume under the project of the directory it is started
		// in, so a resume started from the wrong directory fails to find the session. An empty result means
		// Claude did not start.
		using LaunchClaude = std::function<Core::Ref<CommandTerminal>(
			const std::string& label, const ClaudeCommand& command, const std::filesystem::path& workingDirectory)>;

		SessionsPanel(const Workspace& workspace, const ClaudeSettings& settings, SessionWindows& windows,
					  LaunchClaude launch);
		~SessionsPanel() override;

		SessionsPanel(const SessionsPanel&) = delete;
		SessionsPanel& operator=(const SessionsPanel&) = delete;

		void OnUpdate(Core::Timestep timestep) override;
		void OnImGuiRender() override;

	private:
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
		void RenderRows(const std::vector<VisibleRow>& visible, bool grouped);
		void RenderRow(const SessionSummary& summary);

		static bool VisibleRowPrecedes(const VisibleRow& earlier, const VisibleRow& later);

		const SessionSummary* FindSessionById(const std::string& id) const;

		void StartNewSession();
		void ContinueSession(const SessionSummary& summary);

		std::vector<VisibleRow> VisibleSessions() const;

		std::filesystem::path WorkspaceRootOrCurrentDirectory() const;

		const Workspace& m_Workspace;
		const ClaudeSettings& m_Settings;
		SessionWindows& m_Windows;
		LaunchClaude m_Launch;

		std::filesystem::path m_Projects;
		std::filesystem::path m_CacheFile;

		std::optional<std::string> m_Problem;
		std::optional<std::string> m_LaunchProblem;

		std::chrono::steady_clock::time_point m_NextDirectoryCheck;

		std::map<std::filesystem::path, SessionSummary> m_Sessions;

		LocalClock m_Clock;

		bool m_WorkspaceOnly = true;
		std::string m_Filter;
		bool m_Started = false;
		bool m_Indexing = false;

		bool m_InitialScan = false;
		ScanProgress m_ScanProgress;

		bool m_CacheDirty = false;
		std::chrono::steady_clock::time_point m_LastCacheSave;

		Core::Scope<Core::DirectoryWatcher> m_Watcher;

		// WARNING: Keep last. Its worker is joined before anything above goes away.
		SessionIndex m_Index;
	};
}
