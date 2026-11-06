#pragma once

#include "Core/Git/Lines/AncestryGraph.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Core {

	struct LineDistance
	{
		std::size_t Ahead = 0;                // commits `from` reaches that `to` does not
		std::size_t Behind = 0;               // commits `to` reaches that `from` does not
		std::optional<std::int64_t> ForkTime; // committer time of the newest commit both reach; empty when none
		bool ForkOnLine = false;              // that commit is on `to`'s first-parent chain

		// Of Behind, those on `to`'s first-parent chain: the rows a first-parent history of `to` has that `from` lacks.
		std::size_t BehindOnFirstParents = 0;

		friend bool operator==(const LineDistance&, const LineDistance&) = default;
	};

	// One more than the highest of a commit's parents', so every descendant has a higher generation, whatever the
	// clocks said. The questions below walk in this order instead of by time.
	std::vector<std::uint32_t> GenerationsOf(const AncestryGraph& graph);

	// Per commit, whether a tip in `tips` reaches it (a tip reaches itself); a tip the graph lacks (kNoCommit) is skipped.
	std::vector<bool> CommitsReachableFrom(const AncestryGraph& graph, std::span<const AncestryGraph::Index> tips);

	// The walk ends where the two histories meet, so it touches the commits it counts and the edge of what both
	// reach. The overload without generations computes them over the whole graph first; a caller asking many
	// questions computes them once.
	LineDistance MeasureDistance(const AncestryGraph& graph, std::span<const std::uint32_t> generations, AncestryGraph::Index from,
								 AncestryGraph::Index to);
	LineDistance MeasureDistance(const AncestryGraph& graph, AncestryGraph::Index from, AncestryGraph::Index to);

	// For each tip, whether it reaches `commit` (a tip reaches itself). A tip's walk skips commits whose
	// generation is below the commit's, and commits an earlier tip's walk already found not to lead to it.
	std::vector<bool> TipsHolding(const AncestryGraph& graph, std::span<const std::uint32_t> generations, AncestryGraph::Index commit,
								  std::span<const AncestryGraph::Index> tips);
	std::vector<bool> TipsHolding(const AncestryGraph& graph, AncestryGraph::Index commit, std::span<const AncestryGraph::Index> tips);
}
