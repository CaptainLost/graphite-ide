#include "Git/Worktrees/LookedAtWorktree.h"

#include "Git/Worktrees/WorktreeChanges.h"
#include "Git/Worktrees/WorktreesModel.h"

#include <filesystem>

namespace Graphite {

	LookedAtWorktree::LookedAtWorktree(WorktreeChanges& changes, WorktreesModel& counts)
		: m_Changes(changes), m_Counts(counts)
	{
		m_Changes.OnWatchFired = [this](const std::filesystem::path& worktree) { m_Counts.RecountNow(worktree); };
	}

	LookedAtWorktree::~LookedAtWorktree()
	{
		m_Changes.OnWatchFired = nullptr;
	}

	void LookedAtWorktree::Update(Core::Timestep timestep)
	{
		m_Changes.OnUpdate(timestep);
		m_Counts.SetLookedAt(m_Changes.LookedAt());
	}
}
