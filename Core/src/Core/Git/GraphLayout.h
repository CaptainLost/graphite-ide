#pragma once

#include "Core/Git/Commit.h"

#include <cstddef>
#include <vector>

namespace Core {

	struct GraphNode
	{
		Oid Id;

		// Index into the commit list this layout was built from.
		std::size_t Row = 0;

		// Column, counted from the left.
		std::size_t Lane = 0;
	};

	// A line from a commit down to one of its parents. Both ends are rows in the same
	// layout, so the widget never has to look a commit up while drawing.
	struct GraphEdge
	{
		std::size_t FromRow = 0;
		std::size_t FromLane = 0;
		std::size_t ToRow = 0;
		std::size_t ToLane = 0;
	};

	struct CommitGraph
	{
		std::vector<GraphNode> Nodes;
		std::vector<GraphEdge> Edges;
		std::size_t LaneCount = 0;
	};

	// Assigns each commit a lane so that a branch keeps one column for as long as it
	// lasts, and relies on a merge's first parent being the branch it was made on.
	//
	// Precondition: `commits` must be in ancestry order — every commit's parents appear
	// below it in the list. Newest-first by date is not the same claim and is not
	// sufficient: clock skew can put a parent above its child even in a newest-first
	// sort. Violating this produces an upward edge, a lane reservation that is never
	// released, and a `ToLane` that disagrees with the target node's own lane. A revwalk
	// sorted with `GIT_SORT_TOPOLOGICAL` satisfies this.
	//
	// A lane is released at the child's row, the moment the child that was waiting for
	// it is drawn — not lower down, and not only once every branch on it has ended. A
	// branch that reconverges leftwards frees its column immediately, and an unrelated
	// branch below may occupy that column from the very next row down. A caller drawing
	// the graph must route a connector out of its source lane at the source row, rather
	// than running it down that lane, or it will overdraw a column some other commit has
	// since reused.
	//
	// A parent outside the list — the page of history ended before it — reserves no
	// lane and produces no edge: a reservation that can never be released by a commit
	// that never arrives must not consume a column for the rest of the layout.
	CommitGraph LayoutCommits(const std::vector<CommitSummary>& commits);
}
