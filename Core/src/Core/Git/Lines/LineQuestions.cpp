#include "corepch.h"

#include "Core/Git/Lines/LineQuestions.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace Core {

	namespace {

		using Index = AncestryGraph::Index;

		constexpr std::uint8_t kFromFrom = 1;
		constexpr std::uint8_t kFromTo = 2;
		constexpr std::uint8_t kFromBoth = kFromFrom | kFromTo;

		enum class Visit : std::uint8_t
		{
			NotSeen,
			Open,
			Done
		};

		void PushUnseenParents(const AncestryGraph& graph, const std::vector<Visit>& visits, Index commit, std::vector<Index>& pending)
		{
			for (const Index parent : graph.ParentsOf(commit))
			{
				if (visits[parent] == Visit::NotSeen)
				{
					pending.push_back(parent);
				}
			}
		}

		std::uint32_t HighestGenerationOfParents(const AncestryGraph& graph, const std::vector<std::uint32_t>& generations, Index commit)
		{
			std::uint32_t highest = 0;

			for (const Index parent : graph.ParentsOf(commit))
			{
				highest = std::max(highest, generations[parent]);
			}

			return highest;
		}

		struct QueuedCommit
		{
			std::uint32_t Generation = 0;
			Index Commit = AncestryGraph::kNoCommit;
		};

		bool HasLowerGeneration(const QueuedCommit& first, const QueuedCommit& second)
		{
			return first.Generation < second.Generation;
		}

		// Marks only the commits the walk reaches, so a question about a short stretch does not pay for the graph's size.
		struct DistanceWalk
		{
			const AncestryGraph& Graph;
			std::span<const std::uint32_t> Generations;
			std::unordered_map<Index, std::uint8_t> Marks;
			std::vector<QueuedCommit> Queue; // a heap, highest generation first
			std::size_t QueuedOneSided = 0;
			std::optional<std::int64_t> NewestSharedTime;
			Index NewestShared = AncestryGraph::kNoCommit;
		};

		void NoteShared(DistanceWalk& walk, Index commit)
		{
			const std::int64_t time = walk.Graph.TimeOf(commit);

			if (!walk.NewestSharedTime.has_value() || time > *walk.NewestSharedTime)
			{
				walk.NewestSharedTime = time;
				walk.NewestShared = commit;
			}
		}

		// A commit is queued once, when first reached. It is never reached again after it leaves the queue: all its
		// children have higher generations and left before it.
		void Reach(DistanceWalk& walk, Index commit, std::uint8_t from)
		{
			std::uint8_t& marks = walk.Marks[commit];
			const std::uint8_t before = marks;
			marks = static_cast<std::uint8_t>(before | from);

			if (marks == before)
			{
				return;
			}

			if (before == 0)
			{
				walk.Queue.push_back(QueuedCommit{.Generation = walk.Generations[commit], .Commit = commit});
				std::push_heap(walk.Queue.begin(), walk.Queue.end(), HasLowerGeneration);
				++walk.QueuedOneSided;
			}

			if (marks == kFromBoth)
			{
				--walk.QueuedOneSided;
				NoteShared(walk, commit);
			}
		}

		// After a finished distance walk the marks are final for every commit `to` reaches and `from` does not: each was
		// taken from the queue, and its first parent reached. The count stops at the first commit `from` reaches too.
		std::size_t CountOnlyToAlongFirstParents(const DistanceWalk& walk, Index to)
		{
			std::size_t count = 0;
			Index commit = to;

			while (true)
			{
				const std::unordered_map<Index, std::uint8_t>::const_iterator found = walk.Marks.find(commit);

				if (found == walk.Marks.end() || found->second != kFromTo)
				{
					return count;
				}

				++count;

				const std::span<const Index> parents = walk.Graph.ParentsOf(commit);

				if (parents.empty())
				{
					return count;
				}

				commit = parents.front();
			}
		}

		// A first-parent step lowers the generation, so the walk ends once it is no higher than the target: at most
		// the generations between the two.
		bool IsOnFirstParentChain(const AncestryGraph& graph, std::span<const std::uint32_t> generations, Index target, Index start)
		{
			Index commit = start;

			while (commit != target && generations[commit] > generations[target])
			{
				const std::span<const Index> parents = graph.ParentsOf(commit);

				if (parents.empty())
				{
					return false;
				}

				commit = parents.front();
			}

			return commit == target;
		}
	}

	// Depth first on an explicit stack: a first-parent chain can be hundreds of thousands long.
	std::vector<std::uint32_t> GenerationsOf(const AncestryGraph& graph)
	{
		std::vector<std::uint32_t> generations(graph.Size(), 0);
		std::vector<Visit> visits(graph.Size(), Visit::NotSeen);
		std::vector<Index> pending;

		for (Index start = 0; start < graph.Size(); ++start)
		{
			if (visits[start] == Visit::Done)
			{
				continue;
			}

			pending.push_back(start);

			while (!pending.empty())
			{
				const Index commit = pending.back();

				if (visits[commit] == Visit::NotSeen)
				{
					visits[commit] = Visit::Open;
					PushUnseenParents(graph, visits, commit, pending);
					continue;
				}

				pending.pop_back();

				if (visits[commit] == Visit::Open)
				{
					generations[commit] = HighestGenerationOfParents(graph, generations, commit) + 1;
					visits[commit] = Visit::Done;
				}
			}
		}

		return generations;
	}

	std::vector<bool> CommitsReachableFrom(const AncestryGraph& graph, std::span<const Index> tips)
	{
		std::vector<bool> reached(graph.Size(), false);
		std::vector<Index> pending;

		for (const Index tip : tips)
		{
			if (tip != AncestryGraph::kNoCommit && !reached[tip])
			{
				reached[tip] = true;
				pending.push_back(tip);
			}
		}

		while (!pending.empty())
		{
			const Index commit = pending.back();
			pending.pop_back();

			for (const Index parent : graph.ParentsOf(commit))
			{
				if (!reached[parent])
				{
					reached[parent] = true;
					pending.push_back(parent);
				}
			}
		}

		return reached;
	}

	// Walks from both tips together, highest generation first, until every queued commit is reached from both: the
	// rest is shared history. A skewed clock cannot end the walk before a commit's last child has been seen.
	LineDistance MeasureDistance(const AncestryGraph& graph, std::span<const std::uint32_t> generations, Index from, Index to)
	{
		DistanceWalk walk{.Graph = graph, .Generations = generations};
		Reach(walk, from, kFromFrom);
		Reach(walk, to, kFromTo);
		LineDistance distance;

		while (walk.QueuedOneSided > 0 && !walk.Queue.empty())
		{
			std::pop_heap(walk.Queue.begin(), walk.Queue.end(), HasLowerGeneration);
			const Index commit = walk.Queue.back().Commit;
			walk.Queue.pop_back();
			const std::uint8_t marks = walk.Marks[commit];

			if (marks != kFromBoth)
			{
				--walk.QueuedOneSided;

				if (marks == kFromFrom)
				{
					++distance.Ahead;
				}
				else
				{
					++distance.Behind;
				}
			}

			for (const Index parent : graph.ParentsOf(commit))
			{
				Reach(walk, parent, marks);
			}
		}

		distance.ForkTime = walk.NewestSharedTime;
		distance.ForkOnLine = walk.NewestShared != AncestryGraph::kNoCommit && IsOnFirstParentChain(graph, generations, walk.NewestShared, to);
		distance.BehindOnFirstParents = CountOnlyToAlongFirstParents(walk, to);
		return distance;
	}

	LineDistance MeasureDistance(const AncestryGraph& graph, Index from, Index to)
	{
		return MeasureDistance(graph, GenerationsOf(graph), from, to);
	}

	std::vector<bool> TipsHolding(const AncestryGraph& graph, std::span<const std::uint32_t> generations, Index commit,
								  std::span<const Index> tips)
	{
		std::vector<bool> holding(tips.size(), false);
		std::unordered_set<Index> leadsNowhere; // seen by walks that ended without finding `commit`
		std::unordered_set<Index> seen;
		std::vector<Index> pending;
		const std::uint32_t lowest = generations[commit];

		for (std::size_t position = 0; position < tips.size(); ++position)
		{
			const Index tip = tips[position];

			if (generations[tip] < lowest || leadsNowhere.contains(tip))
			{
				continue;
			}

			seen.clear();
			seen.insert(tip);
			pending.assign(1, tip);
			bool found = false;

			while (!pending.empty() && !found)
			{
				const Index current = pending.back();
				pending.pop_back();
				found = current == commit;

				for (const Index parent : graph.ParentsOf(current))
				{
					if (!found && generations[parent] >= lowest && !leadsNowhere.contains(parent) && seen.insert(parent).second)
					{
						pending.push_back(parent);
					}
				}
			}

			holding[position] = found;

			if (!found)
			{
				leadsNowhere.insert(seen.begin(), seen.end());
			}
		}

		return holding;
	}

	std::vector<bool> TipsHolding(const AncestryGraph& graph, Index commit, std::span<const Index> tips)
	{
		return TipsHolding(graph, GenerationsOf(graph), commit, tips);
	}
}
