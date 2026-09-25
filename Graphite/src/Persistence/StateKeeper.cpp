#include "Persistence/StateKeeper.h"

#include <utility>

namespace Graphite {

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

	void StateKeeper::Observe(double elapsedSeconds, const GlobalState& global,
							  const std::optional<WorkspaceState>& workspace)
	{
		m_GlobalSeen = global;

		if (!m_Root.empty() && workspace)
		{
			m_WorkspaceSeen = *workspace;
		}

		if (!HasUnwritten())
		{
			m_Waiting = false;
			return;
		}

		// The frame that brought the first change does not count towards the delay: it is
		// measured from the change, not from whatever frame came before it.
		if (!m_Waiting)
		{
			m_Waiting = true;
			m_Waited = 0.0;
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

	void StateKeeper::MoveWorkspace(std::filesystem::path to)
	{
		if (m_Root.empty())
		{
			return;
		}

		m_Root = std::move(to);
		// Nothing has been written at the new place yet.
		m_WorkspaceWritten.reset();
	}

	bool StateKeeper::HasUnwritten() const
	{
		const bool global = m_GlobalSeen && m_GlobalSeen != m_GlobalWritten;
		const bool workspace = !m_Root.empty() && m_WorkspaceSeen && m_WorkspaceSeen != m_WorkspaceWritten;

		return global || workspace;
	}

	void StateKeeper::WriteGlobalIfChanged()
	{
		if (m_GlobalSeen && m_GlobalSeen != m_GlobalWritten)
		{
			m_WriteGlobal(*m_GlobalSeen);
			m_GlobalWritten = m_GlobalSeen;
		}
	}

	void StateKeeper::WriteWorkspaceIfChanged()
	{
		if (!m_Root.empty() && m_WorkspaceSeen && m_WorkspaceSeen != m_WorkspaceWritten)
		{
			m_WriteWorkspace(m_Root, *m_WorkspaceSeen);
			m_WorkspaceWritten = m_WorkspaceSeen;
		}
	}
}
