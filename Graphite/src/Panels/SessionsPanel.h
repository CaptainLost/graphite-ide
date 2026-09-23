#pragma once

#include "Sessions/ClaudeLaunch.h"
#include "Sessions/SessionIndex.h"

#include "Core/Base/Base.h"
#include "Core/Fs/DirectoryWatcher.h"
#include "Core/UI/Panel.h"

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
		// Opens `command` in a terminal tab labelled `label`; false when it could not start.
		using LaunchClaude = std::function<bool(const std::string& label, const ClaudeCommand& command)>;

		SessionsPanel(const Workspace& workspace, const ClaudeSettings& settings, SessionWindows& windows,
					  LaunchClaude launch);

		void OnUpdate(Core::Timestep ts) override;
		void OnImGuiRender() override;

	private:
		void StartIndexing();
		void TakeFromIndex();
		void TakeFromWatcher();
		void SaveCacheWhenSettled();

		void RenderToolbar();
		void RenderList();
		void RenderRow(const SessionSummary& summary);

		void StartNewSession();
		void ContinueSession(const SessionSummary& summary);

		std::vector<const SessionSummary*> VisibleSessions() const;

		const Workspace& m_Workspace;
		const ClaudeSettings& m_Settings;
		SessionWindows& m_Windows;
		LaunchClaude m_Launch;

		std::filesystem::path m_Projects;
		std::filesystem::path m_CacheFile;

		// Why there is nothing to show, when there is nothing: no Claude directory, no program.
		std::optional<std::string> m_Problem;
		std::optional<std::string> m_LaunchProblem;

		std::map<std::filesystem::path, SessionSummary> m_Sessions;

		bool m_WorkspaceOnly = true;
		std::string m_Filter;
		bool m_Started = false;
		bool m_CacheDirty = false;

		Core::Scope<Core::DirectoryWatcher> m_Watcher;

		// Last, deliberately: its worker is joined before anything above goes away.
		SessionIndex m_Index;
	};
}
