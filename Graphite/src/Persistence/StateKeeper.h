#pragma once

#include "Persistence/PersistedState.h"

#include <filesystem>
#include <functional>
#include <optional>

namespace Graphite {

	// Decides when state is written. Shown a snapshot every frame; writes a part only once it
	// differs from what was last written and has gone on differing for kDelaySeconds, so a crash
	// loses at most that much and dragging the window does not write a file per frame. Each part
	// is written on its own: moving the caret never rewrites the global settings file.
	class StateKeeper
	{
	public:
		using GlobalWriter = std::function<void(const GlobalState&)>;
		using WorkspaceWriter =
			std::function<void(const std::filesystem::path& root, const WorkspaceState&)>;

		static constexpr double kDelaySeconds = 2.0;

		StateKeeper(GlobalWriter writeGlobal, WorkspaceWriter writeWorkspace);

		// What is already on disk, so a launch that changes nothing writes nothing.
		void AdoptGlobal(GlobalState stored);

		// Starts keeping `root`'s tabs. A folder still being kept is closed first.
		void AdoptWorkspace(std::filesystem::path root, WorkspaceState stored);

		// `workspace` is ignored while no folder is being kept.
		void Observe(double elapsedSeconds, const GlobalState& global,
					 const std::optional<WorkspaceState>& workspace);

		// Writes every part that differs, now.
		void Flush();

		// Writes the folder's part if it differs, then stops keeping it. Uses the last snapshot
		// observed, so it is right to call after the folder's documents are already closed.
		void CloseWorkspace();

		void MoveWorkspace(std::filesystem::path to);

	private:
		bool HasUnwritten() const;
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
