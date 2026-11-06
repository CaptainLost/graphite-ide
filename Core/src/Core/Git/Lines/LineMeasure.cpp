#include "corepch.h"

#include "Core/Git/Lines/LineMeasure.h"

#include "Core/Git/Lines/LineThresholds.h"

#include <algorithm>
#include <vector>

namespace Core {

	namespace {

		using Index = AncestryGraph::Index;

		Index FirstParentOf(const AncestryGraph& graph, Index commit)
		{
			const std::span<const Index> parents = graph.ParentsOf(commit);
			return parents.empty() ? AncestryGraph::kNoCommit : parents.front();
		}

		bool IsMerge(const AncestryGraph& graph, Index commit)
		{
			return graph.ParentsOf(commit).size() >= 2;
		}

		// Counts each author once.
		class AuthorCount
		{
		public:
			explicit AuthorCount(const AncestryGraph& graph) : m_seen(graph.AuthorKeys().size(), false) {}

			void Add(std::uint32_t author)
			{
				if (!m_seen[author])
				{
					m_seen[author] = true;
					++m_count;
				}
			}

			std::size_t Count() const { return m_count; }

		private:
			std::vector<bool> m_seen; // per author
			std::size_t m_count = 0;
		};

		// The newest minus the oldest of the times added, whatever their order; 0 before any is added.
		class TimeSpan
		{
		public:
			void Add(std::int64_t time)
			{
				m_oldest = m_any ? std::min(m_oldest, time) : time;
				m_newest = m_any ? std::max(m_newest, time) : time;
				m_any = true;
			}

			std::int64_t Length() const { return m_newest - m_oldest; }

		private:
			std::int64_t m_oldest = 0;
			std::int64_t m_newest = 0;
			bool m_any = false;
		};

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

				current = FirstParentOf(graph, current);
			}

			return false;
		}

		bool SameLine(const AncestryGraph& graph, Index first, Index second)
		{
			const bool firstIsNewer = graph.TimeOf(first) >= graph.TimeOf(second);
			return firstIsNewer ? OnFirstParentChainOf(graph, second, first) : OnFirstParentChainOf(graph, first, second);
		}

		bool IsOnAnyLineSeen(const AncestryGraph& graph, const std::vector<Index>& linesSeen, Index candidate)
		{
			for (const Index seen : linesSeen)
			{
				if (SameLine(graph, seen, candidate))
				{
					return true;
				}
			}

			return false;
		}

		std::vector<bool> MarkBaseChains(const AncestryGraph& graph, std::span<const Index> baseTips)
		{
			std::vector<bool> onBase(graph.Size(), false);

			for (const Index tip : baseTips)
			{
				for (Index current = tip; current != AncestryGraph::kNoCommit && !onBase[current]; current = FirstParentOf(graph, current))
				{
					onBase[current] = true;
				}
			}

			return onBase;
		}

		bool IsMergeSince(const AncestryGraph& graph, Index commit, std::int64_t windowStart)
		{
			return IsMerge(graph, commit) && graph.TimeOf(commit) >= windowStart;
		}

		bool AnyMergeSince(const AncestryGraph& graph, const std::vector<Index>& stretch, std::int64_t windowStart)
		{
			for (const Index commit : stretch)
			{
				if (IsMergeSince(graph, commit, windowStart))
				{
					return true;
				}
			}

			return false;
		}

		// A merged parent on a base's first-parent line is the trunk merged into one's own branch, not a line merged in.
		// Counting stops at kMergedLinesForBase, which is all the rules ask.
		std::size_t CountMergedLines(const AncestryGraph& graph, const std::vector<Index>& stretch, const std::vector<bool>& onBase,
									 std::int64_t windowStart)
		{
			std::vector<Index> lines; // one commit per distinct merged line

			for (const Index commit : stretch)
			{
				if (!IsMergeSince(graph, commit, windowStart))
				{
					continue;
				}

				const std::span<const Index> parents = graph.ParentsOf(commit);

				for (std::size_t i = 1; i < parents.size(); ++i)
				{
					const Index candidate = parents[i];

					if (onBase[candidate] || IsOnAnyLineSeen(graph, lines, candidate))
					{
						continue;
					}

					lines.push_back(candidate);

					if (lines.size() >= kMergedLinesForBase)
					{
						return lines.size();
					}
				}
			}

			return lines.size();
		}

		// Who made the merged lines' tips, and over how long the merges that brought them in were made: one person
		// merging their own pieces within a day is not several people's lines landing on a shared one.
		void MeasureMergedTips(const AncestryGraph& graph, const std::vector<Index>& stretch, const std::vector<bool>& onBase,
							   std::int64_t windowStart, LineQualities& qualities)
		{
			AuthorCount authors(graph);
			TimeSpan merges;

			for (const Index commit : stretch)
			{
				if (!IsMergeSince(graph, commit, windowStart))
				{
					continue;
				}

				const std::span<const Index> parents = graph.ParentsOf(commit);
				bool bringsALine = false;

				for (std::size_t i = 1; i < parents.size(); ++i)
				{
					if (!onBase[parents[i]])
					{
						bringsALine = true;
						authors.Add(graph.AuthorOf(parents[i]));
					}
				}

				if (bringsALine)
				{
					merges.Add(graph.TimeOf(commit));
				}
			}

			qualities.MergedAuthors = authors.Count();
			qualities.MergeSpan = merges.Length();
		}

		void MeasureMerges(const AncestryGraph& graph, const std::vector<Index>& stretch, std::span<const Index> baseTips, std::int64_t now,
						   LineQualities& qualities)
		{
			const std::int64_t windowStart = now - kActiveWindow;

			// Most lines have no merge to judge, and marking the bases' chains walks the whole trunk.
			if (!AnyMergeSince(graph, stretch, windowStart))
			{
				return;
			}

			const std::vector<bool> onBase = MarkBaseChains(graph, baseTips);
			qualities.MergedLines = CountMergedLines(graph, stretch, onBase, windowStart);

			if (qualities.MergedLines >= kMergedLinesForBase)
			{
				MeasureMergedTips(graph, stretch, onBase, windowStart, qualities);
			}
		}

		// Work someone else put on the line, a pull request the service merged or a commit a maintainer picked, is work
		// a team shares; work its author committed is most often a topic of their own. A merge only joins work made
		// elsewhere, so its author is not counted, though its time is.
		bool CountsTowardAuthors(const AncestryGraph& graph, Index commit)
		{
			return !IsMerge(graph, commit) && !graph.CommittedByAuthor(commit);
		}

		void MeasureOwnWork(const AncestryGraph& graph, const std::vector<Index>& stretch, const std::vector<bool>& reachedByOtherBases,
							LineQualities& qualities)
		{
			AuthorCount authors(graph);
			TimeSpan times;

			for (const Index commit : stretch)
			{
				if (reachedByOtherBases[commit])
				{
					continue;
				}

				++qualities.Commits;
				times.Add(graph.TimeOf(commit));

				if (CountsTowardAuthors(graph, commit))
				{
					authors.Add(graph.AuthorOf(commit));
				}
			}

			qualities.Authors = authors.Count();
			qualities.Span = times.Length();
		}

		bool QualifiesByAuthors(const LineQualities& qualities)
		{
			return qualities.Authors >= kAuthorsForBase && qualities.Span >= kSpanForBase && qualities.Commits >= kCommitsForBase;
		}
	}

	std::vector<AncestryGraph::Index> StretchOf(const AncestryGraph& graph, const LineSplit& split, std::uint32_t line)
	{
		std::vector<Index> stretch;

		if (line == kNoLine || line >= split.Lines.size())
		{
			return stretch;
		}

		for (Index commit = split.Lines[line].Top; commit != AncestryGraph::kNoCommit && split.LineOf[commit] == line;
			 commit = FirstParentOf(graph, commit))
		{
			stretch.push_back(commit);
		}

		return stretch;
	}

	LineQualities MeasureLine(const AncestryGraph& graph, const LineSplit& split, std::uint32_t line,
							  std::span<const AncestryGraph::Index> baseTips, const std::vector<bool>& reachedByOtherBases, std::int64_t now)
	{
		const std::vector<Index> stretch = StretchOf(graph, split, line);
		LineQualities qualities;
		MeasureOwnWork(graph, stretch, reachedByOtherBases, qualities);
		MeasureMerges(graph, stretch, baseTips, now, qualities);
		return qualities;
	}

	std::size_t CountStretchAuthors(const AncestryGraph& graph, const LineSplit& split, std::uint32_t line)
	{
		AuthorCount authors(graph);

		for (const Index commit : StretchOf(graph, split, line))
		{
			authors.Add(graph.AuthorOf(commit));
		}

		return authors.Count();
	}

	bool QualifiesByMerges(const LineQualities& qualities)
	{
		const bool severalPeoplesLines = qualities.MergedAuthors >= kMergedAuthorsForBase;
		const bool mergedOverAWeek = qualities.MergeSpan >= kSpanForBase;
		return qualities.MergedLines >= kMergedLinesForBase && (severalPeoplesLines || mergedOverAWeek);
	}

	bool QualifiesAsBase(const LineQualities& qualities)
	{
		return QualifiesByMerges(qualities) || QualifiesByAuthors(qualities);
	}
}
