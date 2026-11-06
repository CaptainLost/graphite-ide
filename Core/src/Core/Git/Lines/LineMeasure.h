#pragma once

#include "Core/Git/Lines/AncestryGraph.h"
#include "Core/Git/Lines/LineSplit.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Core {

	// A line's stretch is its commits from its Top down its first parents to its Bottom. Its own commits are those of
	// the stretch no other base reaches: a pull request a base merged that the line was built on, or an old trunk a
	// merge took in, is history a base already holds, not the line's work.
	struct LineQualities
	{
		std::size_t MergedLines = 0;   // distinct lines merged into the stretch within kActiveWindow
		std::size_t MergedAuthors = 0; // distinct authors of those lines' merged tips; measured once MergedLines reaches kMergedLinesForBase
		std::int64_t MergeSpan = 0;    // newest minus oldest of the merges that brought those lines in; likewise
		std::size_t Authors = 0;       // distinct authors of the own commits that are no merge and that someone else committed
		std::int64_t Span = 0;         // newest minus oldest commit time of the own commits, merges included, never negative
		std::size_t Commits = 0;       // the own commits, merges included

		friend bool operator==(const LineQualities&, const LineQualities&) = default;
	};

	// The line's stretch, in that order: Top first, Bottom last. Empty for kNoLine.
	std::vector<AncestryGraph::Index> StretchOf(const AncestryGraph& graph, const LineSplit& split, std::uint32_t line);

	// baseTips as in SplitLines. reachedByOtherBases has exactly one entry per commit of the graph (graph.Size()):
	// whether a base other than the one that owns the line reaches it.
	LineQualities MeasureLine(const AncestryGraph& graph, const LineSplit& split, std::uint32_t line,
							  std::span<const AncestryGraph::Index> baseTips, const std::vector<bool>& reachedByOtherBases, std::int64_t now);

	// Distinct authors of every commit on the line's stretch, merges and work its author committed included: how many
	// people a line holds, as the main-line guess weighs it.
	std::size_t CountStretchAuthors(const AncestryGraph& graph, const LineSplit& split, std::uint32_t line);

	// kMergedLinesForBase or more merged lines, whose tips have kMergedAuthorsForBase or more authors or whose merges
	// span kSpanForBase or more.
	bool QualifiesByMerges(const LineQualities& qualities);

	// QualifiesByMerges, or (kAuthorsForBase+ authors, Span >= kSpanForBase, kCommitsForBase+ commits).
	bool QualifiesAsBase(const LineQualities& qualities);
}
