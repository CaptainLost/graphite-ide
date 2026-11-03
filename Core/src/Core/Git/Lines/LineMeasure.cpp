#include "corepch.h"

#include "Core/Git/Lines/LineMeasure.h"

#include "Core/Git/Lines/LineThresholds.h"

#include <algorithm>
#include <vector>

namespace Core {

	namespace {

		using Index = AncestryGraph::Index;

		// Commit times are only numbers here: a child may be older than its parent. The walk allows a day of
		// skew below its target before it gives up.
		bool OnFirstParentChainOf(const AncestryGraph& graph, Index target, Index start)
		{
			const std::int64_t floor = graph.TimeOf(target) - kSecondsPerDay;
			Index current = start;

			while (current != AncestryGraph::kNoCommit && graph.TimeOf(current) >= floor)
			{
				if (current == target)
				{
					return true;
				}

				const std::span<const Index> parents = graph.ParentsOf(current);
				current = parents.empty() ? AncestryGraph::kNoCommit : parents.front();
			}

			return false;
		}

		bool SameLine(const AncestryGraph& graph, Index first, Index second)
		{
			const bool firstIsNewer = graph.TimeOf(first) >= graph.TimeOf(second);
			return firstIsNewer ? OnFirstParentChainOf(graph, second, first) : OnFirstParentChainOf(graph, first, second);
		}

		std::vector<bool> MarkBaseChains(const AncestryGraph& graph, std::span<const Index> baseTips)
		{
			std::vector<bool> onBase(graph.Size(), false);

			for (const Index tip : baseTips)
			{
				Index current = tip;

				while (current != AncestryGraph::kNoCommit && !onBase[current])
				{
					onBase[current] = true;
					const std::span<const Index> parents = graph.ParentsOf(current);
					current = parents.empty() ? AncestryGraph::kNoCommit : parents.front();
				}
			}

			return onBase;
		}

		std::size_t CountMergedLines(const AncestryGraph& graph, const std::vector<Index>& stretch,
									 std::span<const Index> baseTips, std::int64_t now)
		{
			std::vector<bool> onBase; // built on the first candidate: most lines have no merge to judge
			bool onBaseBuilt = false;
			const std::int64_t windowStart = now - kActiveWindow;
			std::vector<Index> lines; // one commit per distinct merged line

			for (const Index commit : stretch)
			{
				const std::span<const Index> parents = graph.ParentsOf(commit);

				if (parents.size() < 2 || graph.TimeOf(commit) < windowStart)
				{
					continue;
				}

				for (std::size_t i = 1; i < parents.size(); ++i)
				{
					const Index candidate = parents[i];

					if (!onBaseBuilt)
					{
						onBase = MarkBaseChains(graph, baseTips);
						onBaseBuilt = true;
					}

					if (onBase[candidate])
					{
						continue;
					}

					const bool known = std::ranges::any_of(lines, [&](Index seen) { return SameLine(graph, seen, candidate); });

					if (!known)
					{
						lines.push_back(candidate);

						if (lines.size() >= kMergedLinesForBase)
						{
							return lines.size();
						}
					}
				}
			}

			return lines.size();
		}
	}

	LineQualities MeasureLine(const AncestryGraph& graph, const LineSplit& split, std::uint32_t line,
							  std::span<const AncestryGraph::Index> baseTips, std::int64_t now)
	{
		LineQualities qualities;

		if (line == kNoLine || line >= split.Lines.size())
		{
			return qualities;
		}

		const SplitLine& own = split.Lines[line];
		std::vector<Index> stretch;

		for (Index commit = own.Top; commit != AncestryGraph::kNoCommit && split.LineOf[commit] == line;)
		{
			stretch.push_back(commit);
			const std::span<const Index> parents = graph.ParentsOf(commit);
			commit = parents.empty() ? AncestryGraph::kNoCommit : parents.front();
		}

		if (stretch.empty())
		{
			return qualities;
		}

		std::vector<bool> authorSeen(graph.AuthorKeys().size(), false);
		std::int64_t oldest = graph.TimeOf(stretch.front());
		std::int64_t newest = oldest;

		for (const Index commit : stretch)
		{
			const std::uint32_t author = graph.AuthorOf(commit);

			if (!authorSeen[author])
			{
				authorSeen[author] = true;
				++qualities.Authors;
			}

			oldest = std::min(oldest, graph.TimeOf(commit));
			newest = std::max(newest, graph.TimeOf(commit));
		}

		qualities.Span = newest - oldest;
		qualities.Commits = stretch.size();
		qualities.MergedLines = CountMergedLines(graph, stretch, baseTips, now);
		return qualities;
	}

	bool QualifiesAsBase(const LineQualities& qualities)
	{
		return qualities.MergedLines >= kMergedLinesForBase
			|| (qualities.Authors >= kAuthorsForBase && qualities.Span >= kSpanForBase && qualities.Commits >= kCommitsForBase);
	}

	bool IsAncestor(const AncestryGraph& graph, AncestryGraph::Index ancestor, AncestryGraph::Index descendant)
	{
		if (ancestor == AncestryGraph::kNoCommit || descendant == AncestryGraph::kNoCommit)
		{
			return false;
		}

		// A commit older than the ancestor by more than a day cannot lead to it unless clocks are far off; the
		// day is the skew allowance, and it ends the walk early on a long history.
		const std::int64_t floor = graph.TimeOf(ancestor) - kSecondsPerDay;
		std::vector<bool> visited(graph.Size(), false);
		std::vector<Index> pending{descendant};
		visited[descendant] = true;

		while (!pending.empty())
		{
			const Index current = pending.back();
			pending.pop_back();

			if (current == ancestor)
			{
				return true;
			}

			if (graph.TimeOf(current) < floor)
			{
				continue;
			}

			for (const Index parent : graph.ParentsOf(current))
			{
				if (!visited[parent])
				{
					visited[parent] = true;
					pending.push_back(parent);
				}
			}
		}

		return false;
	}
}
