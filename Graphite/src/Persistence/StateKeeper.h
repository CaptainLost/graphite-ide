#pragma once

#include "Persistence/PersistedState.h"

#include <filesystem>
#include <functional>
#include <optional>

namespace Graphite {

	class StateKeeper
	{
	public:
		using GlobalWriter = std::function<void(const GlobalState&)>;
		using WorkspaceWriter =
			std::function<void(const std::filesystem::path& root, const WorkspaceState&)>;

		static constexpr double kDelaySeconds = 2.0;

		StateKeeper(GlobalWriter writeGlobal, WorkspaceWriter writeWorkspace);

		void AdoptGlobal(GlobalState stored);

		void AdoptWorkspace(std::filesystem::path root, WorkspaceState stored);

		void Observe(double elapsedSeconds, const GlobalState& globalState,
					 const std::optional<WorkspaceState>& workspaceState);

		void Flush();

		void CloseWorkspace();

		void MoveWorkspace(std::filesystem::path newRoot);

	private:
		bool HasUnwritten() const;
		bool HasUnwrittenGlobal() const;
		bool HasUnwrittenWorkspace() const;
		void WriteGlobalIfChanged();
		void WriteWorkspaceIfChanged();

		GlobalWriter m_WriteGlobal;
		WorkspaceWriter m_WriteWorkspace;

		std::optional<GlobalState> m_GlobalSeen;
		std::optional<GlobalState> m_GlobalWritten;

		std::filesystem::path m_Root;
		std::optional<WorkspaceState> m_WorkspaceSeen;
		std::optional<WorkspaceState> m_WorkspaceWritten;

		bool m_Waiting = false;
		double m_Waited = 0.0;
	};
}
