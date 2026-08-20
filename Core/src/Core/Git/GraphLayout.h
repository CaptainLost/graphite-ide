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
	// lasts. Takes the commits in the order they will be drawn — newest first — and
	// relies on a merge's first parent being the branch it was made on.
	//
	// Edges to commits outside the list are dropped: a page of history ends somewhere,
	// and an edge to a row that was never loaded has nothing to point at.
	CommitGraph LayoutCommits(const std::vector<CommitSummary>& commits);
}
