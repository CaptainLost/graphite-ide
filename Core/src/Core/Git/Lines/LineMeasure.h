#pragma once

#include "Core/Git/Lines/AncestryGraph.h"
#include "Core/Git/Lines/LineSplit.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace Core {

	struct LineQualities
	{
		std::size_t MergedLines = 0; // distinct lines merged into the line's own stretch within kActiveWindow
		std::size_t Authors = 0;     // distinct authors of the line's own commits
		std::int64_t Span = 0;       // newest minus oldest commit time of the line's own commits, never negative
		std::size_t Commits = 0;     // the line's own commits

		friend bool operator==(const LineQualities&, const LineQualities&) = default;
	};

	// The line's own stretch is its commits, Bottom to Top. baseTips as in SplitLines.
	LineQualities MeasureLine(const AncestryGraph& graph, const LineSplit& split, std::uint32_t line,
							  std::span<const AncestryGraph::Index> baseTips, std::int64_t now);

	// kMergedLinesForBase or more merged lines, or (kAuthorsForBase+ authors, Span >= kSpanForBase, kCommitsForBase+ commits).
	bool QualifiesAsBase(const LineQualities& qualities);

	// True when `ancestor` is reachable from `descendant` (a commit is its own ancestor).
	bool IsAncestor(const AncestryGraph& graph, AncestryGraph::Index ancestor, AncestryGraph::Index descendant);
}
