#pragma once

#include "Core/Base/Base.h"
#include "Core/Base/NonCopyable.h"
#include "Core/Base/Timestep.h"
#include "Core/Git/Head.h"

namespace Graphite {

	class WorktreeChanges;
	class WorktreesModel;

	// The worktree counts leave the worktree looked at to its watch: they never poll nor sweep it, and count it again
	// each time its watch fires, so its row keeps its count, activity and live marker while an agent works there. A HEAD
	// the watch reads that the list does not hold, detached or on another branch, asks the list again: no ref of the open
	// folder moves for it, so the branch snapshot never would.
	class LookedAtWorktree : Core::NonCopyable
	{
	public:
		// WARNING: Sets changes.OnWatchFired, which calls into counts; both must outlive this.
		LookedAtWorktree(WorktreeChanges& changes, WorktreesModel& counts);
		~LookedAtWorktree();

		// Updates the changes, then tells the counts which worktree is looked at.
		void Update(Core::Timestep timestep);

	private:
		void ListAgainWhenHeadMoved();

		WorktreeChanges& m_Changes;
		WorktreesModel& m_Counts;

		// The watched worktree's HEAD last held against the list: each read is compared once.
		Core::Ref<const Core::HeadState> m_HeadCompared;
	};
}
