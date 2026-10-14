#include "Persistence/StateKeeper.h"

#include <utility>

namespace Graphite {

	namespace {
		constexpr double kNothingWaited = 0.0;
	}

	StateKeeper::StateKeeper(GlobalWriter writeGlobal, WorkspaceWriter writeWorkspace)
		: m_WriteGlobal(std::move(writeGlobal)), m_WriteWorkspace(std::move(writeWorkspace))
	{
	}

	void StateKeeper::AdoptGlobal(GlobalState stored)
	{
		m_GlobalSeen = stored;
		m_GlobalWritten = std::move(stored);
	}

	void StateKeeper::AdoptWorkspace(std::filesystem::path root, WorkspaceState stored)
	{
		if (!m_Root.empty())
		{
			CloseWorkspace();
		}

		m_Root = std::move(root);
		m_WorkspaceSeen = stored;
		m_WorkspaceWritten = std::move(stored);
	}

	void StateKeeper::Observe(double elapsedSeconds, const GlobalState& globalState,
							  const std::optional<WorkspaceState>& workspaceState)
	{
		m_GlobalSeen = globalState;

		if (!m_Root.empty() && workspaceState)
		{
			m_WorkspaceSeen = *workspaceState;
		}

		if (!HasUnwritten())
		{
			m_Waiting = false;
			return;
		}

		const bool firstFrameOfTheChange = !m_Waiting;

		if (firstFrameOfTheChange)
		{
			m_Waiting = true;
			m_Waited = kNothingWaited;
			return;
		}

		m_Waited += elapsedSeconds;

		if (m_Waited >= kDelaySeconds)
		{
			Flush();
		}
	}

	void StateKeeper::Flush()
	{
		WriteGlobalIfChanged();
		WriteWorkspaceIfChanged();
		m_Waiting = false;
	}

	void StateKeeper::CloseWorkspace()
	{
		WriteWorkspaceIfChanged();

		m_Root.clear();
		m_WorkspaceSeen.reset();
		m_WorkspaceWritten.reset();
	}

	void StateKeeper::MoveWorkspace(std::filesystem::path newRoot)
	{
		if (m_Root.empty())
		{
			return;
		}

		m_Root = std::move(newRoot);
		m_WorkspaceWritten.reset();
	}

	bool StateKeeper::HasUnwritten() const
	{
		return HasUnwrittenGlobal() || HasUnwrittenWorkspace();
	}

	bool StateKeeper::HasUnwrittenGlobal() const
	{
		return m_GlobalSeen && m_GlobalSeen != m_GlobalWritten;
	}

	bool StateKeeper::HasUnwrittenWorkspace() const
	{
		return !m_Root.empty() && m_WorkspaceSeen && m_WorkspaceSeen != m_WorkspaceWritten;
	}

	void StateKeeper::WriteGlobalIfChanged()
	{
		if (HasUnwrittenGlobal())
		{
			m_WriteGlobal(*m_GlobalSeen);
			m_GlobalWritten = m_GlobalSeen;
		}
	}

	void StateKeeper::WriteWorkspaceIfChanged()
	{
		if (HasUnwrittenWorkspace())
		{
			m_WriteWorkspace(m_Root, *m_WorkspaceSeen);
			m_WorkspaceWritten = m_WorkspaceSeen;
		}
	}
}
