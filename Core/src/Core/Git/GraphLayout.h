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

		std::size_t Lane = 0;
	};

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

	// `commits` must list every parent below its child, as a revwalk sorted with
	// GIT_SORT_TOPOLOGICAL does; newest-first by date is not enough, because clock skew can
	// put a parent above its child, and the layout then draws upward edges and never frees
	// their lanes.
	//
	// A lane is freed at the row of the child that was waiting for it, so a commit below may
	// take it from the next row on. A caller drawing the graph must route a connector out of
	// its source lane at the source row rather than run it down that lane.
	CommitGraph LayoutCommits(const std::vector<CommitSummary>& commits);
}
