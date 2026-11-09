#pragma once

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Timestep.h"

namespace Graphite {

	class WorktreeChanges;
	class WorktreesModel;

	// The worktree counts leave the worktree looked at to its watch: they never poll nor sweep it, and count it again
	// each time its watch fires, so its row keeps its count, activity and live marker while an agent works there.
	class LookedAtWorktree : Core::NonCopyable
	{
	public:
		// WARNING: Sets changes.OnWatchFired, which calls into counts; both must outlive this.
		LookedAtWorktree(WorktreeChanges& changes, WorktreesModel& counts);
		~LookedAtWorktree();

		// Updates the changes, then tells the counts which worktree is looked at.
		void Update(Core::Timestep timestep);

	private:
		WorktreeChanges& m_Changes;
		WorktreesModel& m_Counts;
	};
}
