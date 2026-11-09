#include "Git/RepositorySession.h"

#include "Git/Branches/BranchAuthors.h"
#include "Git/Content/ContentModel.h"
#include "Git/GitModel.h"
#include "Git/Lines/LinesModel.h"
#include "Git/Network/NetworkModel.h"
#include "Git/Worktrees/WorktreesModel.h"
#include "Git/WriteConsequences.h"

namespace Graphite {

	RepositorySession::RepositorySession(GitModel& git, WriteConsequences& writes, NetworkModel& network, LinesModel& lines,
										 WorktreesModel& worktrees, ContentModel& content)
		: m_Git(git),
		  m_Writes(writes),
		  m_Network(network),
		  m_Lines(lines),
		  m_Worktrees(worktrees),
		  m_Content(content)
	{
	}

	void RepositorySession::Update(Core::Timestep timestep)
	{
		m_Git.OnUpdate(timestep);

		// WARNING: Right after the model: an expected write must be registered before the watcher reports it, and the
		// watcher never sees `.git/` when the workspace is a subfolder of the repository or a linked worktree, so writes
		// made by this process reach the change markers and blame only from here.
		m_Writes.Apply();

		// WARNING: After the git model, whose update applies Pull's fast-forward for the network model to take.
		m_Network.OnUpdate(timestep);

		m_Lines.Follow(m_Git.GetBranches(), m_Git.GetBranchListingGeneration(), m_Git.GetTags());
		m_Lines.OnUpdate(timestep);
		m_Worktrees.Follow(m_Git.GetBranches(), m_Git.GetBranchListingGeneration(), m_Git.GetWorkingDirectory());
		m_Worktrees.OnUpdate(timestep);

		const Core::Ref<const LineRead> read = m_Lines.GetRead();

		if (read != m_AuthorsHiddenFor)
		{
			m_AuthorsHiddenFor = read;

			if (read)
			{
				m_Git.SetAuthorsHidden(BaseTipsOf(*read));
			}
		}

		m_Content.Follow(read);
		m_Content.OnUpdate(timestep);
	}
}
