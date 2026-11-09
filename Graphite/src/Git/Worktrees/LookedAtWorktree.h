#pragma once

#include "Core/Base/Base.h"
#include "Core/Base/NonCopyable.h"
#include "Core/Base/Timestep.h"
#include "Core/Git/Head.h"

namespace Graphite {

	class WorktreeChanges;
	class WorktreesModel;

	// The worktree counts leave the worktree looked at to its watch: they never poll nor sweep it, and count it again as
	// its watch fires (at most once a poll period, WorktreesModel::RecountNow), so its row keeps its count, activity and
	// live marker while an agent works there. A HEAD the watch reads that the list does not hold, detached or on another
	// branch, or no HEAD at all where no repository is left, asks the list again: no ref of the open folder moves for it.
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

		// A HEAD read that finds no repository at a listed worktree asks the list again, which then says the folder is
		// missing or the worktree gone, as no ref of the open folder moves for it.
		void ListAgainWhenGone();

		WorktreeChanges& m_Changes;
		WorktreesModel& m_Counts;

		// The watched worktree's HEAD last held against the list: each read is compared once.
		Core::Ref<const Core::HeadState> m_HeadCompared;

		// The followed worktree's HEAD read found no repository last frame.
		bool m_HeadGone = false;
	};
}
