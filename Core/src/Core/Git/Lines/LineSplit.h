#pragma once

#include "Core/Git/Lines/AncestryGraph.h"

#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace Core {

	inline constexpr std::uint32_t kNoLine = std::numeric_limits<std::uint32_t>::max();

	struct SplitLine
	{
		AncestryGraph::Index Bottom = AncestryGraph::kNoCommit;
		AncestryGraph::Index Top = AncestryGraph::kNoCommit;
		AncestryGraph::Index Join = AncestryGraph::kNoCommit; // the commit it grew from; kNoCommit for a line from a root
	};

	struct LineSplit
	{
		std::vector<std::uint32_t> LineOf;     // per commit; kNoLine off every tip's first-parent chain
		std::vector<std::uint32_t> PositionOf; // per commit; 0 at its line's bottom, and 0 where LineOf is kNoLine
		std::vector<SplitLine> Lines;
	};

	// tips: every reference's tip. baseTips: the tips of the current bases, a subset of tips, in the order that
	// decides which base keeps the line where two of them fork, and whether a line goes on past a base's tip (only
	// along the chain of a base listed before it). A tip the graph lacks (kNoCommit) is skipped.
	LineSplit SplitLines(const AncestryGraph& graph, std::span<const AncestryGraph::Index> tips,
						 std::span<const AncestryGraph::Index> baseTips);
}
