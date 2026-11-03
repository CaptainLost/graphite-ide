#include "corepch.h"

#include "Core/Git/Lines/LineSplit.h"

#include "Core/Git/Lines/LineThresholds.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <unordered_set>
#include <vector>

// Every walk here is a loop over an explicit list: a first-parent chain can be hundreds of thousands of commits
// long, and recursion along it overflows the stack.
namespace Core {

	namespace {

		using Index = AncestryGraph::Index;

		constexpr std::uint32_t kNoBase = std::numeric_limits<std::uint32_t>::max();
		constexpr std::uint32_t kNoAuthorSet = std::numeric_limits<std::uint32_t>::max();

		bool IsInGraph(const AncestryGraph& graph, Index commit)
		{
			return commit < graph.Size();
		}

		Index FirstParentOf(const AncestryGraph& graph, Index commit)
		{
			const std::span<const Index> parents = graph.ParentsOf(commit);
			return parents.empty() ? AncestryGraph::kNoCommit : parents.front();
		}

		// The commits on some tip's first-parent chain. They form a forest: a commit's path children are the
		// path commits whose first parent it is.
		struct PathTree
		{
			std::vector<bool> IsTip;               // per commit
			std::vector<Index> Roots;              // path commits without a first parent
			std::vector<std::size_t> ChildStarts;  // a commit's path children are Children[ChildStarts[commit] .. ChildStarts[commit + 1])
			std::vector<Index> Children;
			std::vector<Index> ParentsFirst;       // every path commit, each one after its first parent
		};

		std::span<const Index> PathChildrenOf(const PathTree& tree, Index commit)
		{
			const std::size_t begin = tree.ChildStarts[commit];
			return std::span<const Index>(tree.Children).subspan(begin, tree.ChildStarts[commit + 1] - begin);
		}

		std::vector<bool> MarkTips(const AncestryGraph& graph, std::span<const Index> tips)
		{
			std::vector<bool> isTip(graph.Size(), false);

			for (const Index tip : tips)
			{
				if (IsInGraph(graph, tip))
				{
					isTip[tip] = true;
				}
			}

			return isTip;
		}

		// Walks each tip's first parents until the walk reaches a commit an earlier tip already put on a path.
		std::vector<Index> CollectPathCommits(const AncestryGraph& graph, std::span<const Index> tips)
		{
			std::vector<bool> isOnPath(graph.Size(), false);
			std::vector<Index> pathCommits;

			for (const Index tip : tips)
			{
				Index commit = IsInGraph(graph, tip) ? tip : AncestryGraph::kNoCommit;

				while (commit != AncestryGraph::kNoCommit && !isOnPath[commit])
				{
					isOnPath[commit] = true;
					pathCommits.push_back(commit);
					commit = FirstParentOf(graph, commit);
				}
			}

			return pathCommits;
		}

		void LinkPathChildren(const AncestryGraph& graph, const std::vector<Index>& pathCommits, PathTree& tree)
		{
			std::vector<std::size_t> childCounts(graph.Size(), 0);

			for (const Index commit : pathCommits)
			{
				const Index firstParent = FirstParentOf(graph, commit);

				if (firstParent == AncestryGraph::kNoCommit)
				{
					tree.Roots.push_back(commit);
				}
				else
				{
					++childCounts[firstParent];
				}
			}

			tree.ChildStarts.assign(graph.Size() + 1, 0);

			for (Index commit = 0; commit < graph.Size(); ++commit)
			{
				tree.ChildStarts[commit + 1] = tree.ChildStarts[commit] + childCounts[commit];
			}

			std::vector<std::size_t> nextSlot(tree.ChildStarts.begin(), tree.ChildStarts.end() - 1);
			tree.Children.resize(tree.ChildStarts.back());

			for (const Index commit : pathCommits)
			{
				const Index firstParent = FirstParentOf(graph, commit);

				if (firstParent != AncestryGraph::kNoCommit)
				{
					tree.Children[nextSlot[firstParent]] = commit;
					++nextSlot[firstParent];
				}
			}
		}

		void OrderParentsFirst(PathTree& tree)
		{
			std::vector<Index> pending(tree.Roots.begin(), tree.Roots.end());

			while (!pending.empty())
			{
				const Index commit = pending.back();
				pending.pop_back();
				tree.ParentsFirst.push_back(commit);

				for (const Index child : PathChildrenOf(tree, commit))
				{
					pending.push_back(child);
				}
			}
		}

		PathTree BuildPathTree(const AncestryGraph& graph, std::span<const Index> tips)
		{
			PathTree tree;
			tree.IsTip = MarkTips(graph, tips);
			LinkPathChildren(graph, CollectPathCommits(graph, tips), tree);
			OrderParentsFirst(tree);
			return tree;
		}

		// Author sets handed up from a child to its parent. A set its parent drops is emptied and reused, so the
		// whole weighing inserts each path commit's author once.
		class AuthorSetPool
		{
		public:
			std::uint32_t Take()
			{
				if (!m_free.empty())
				{
					const std::uint32_t set = m_free.back();
					m_free.pop_back();
					return set;
				}

				m_sets.emplace_back();
				return static_cast<std::uint32_t>(m_sets.size() - 1);
			}

			void Release(std::uint32_t set)
			{
				m_sets[set].clear();
				m_free.push_back(set);
			}

			// The set's size afterwards.
			std::size_t Add(std::uint32_t set, std::uint32_t author)
			{
				m_sets[set].insert(author);
				return m_sets[set].size();
			}

		private:
			std::vector<std::unordered_set<std::uint32_t>> m_sets;
			std::vector<std::uint32_t> m_free;
		};

		struct PathWeights
		{
			std::vector<std::size_t> TipsAbove;    // per commit: tips on it and on every path above it
			std::vector<std::size_t> AuthorsAbove; // per commit: distinct authors on it and along its heaviest continuation
			std::vector<Index> HeaviestChild;      // per commit; kNoCommit for a commit without path children
		};

		// Most distinct authors, then most tips above, then the newer commit; the lower index settles a tie in
		// all three so that the split never depends on the order the children were found in.
		bool IsHeavier(const AncestryGraph& graph, const PathWeights& weights, Index candidate, Index current)
		{
			if (weights.AuthorsAbove[candidate] != weights.AuthorsAbove[current])
			{
				return weights.AuthorsAbove[candidate] > weights.AuthorsAbove[current];
			}

			if (weights.TipsAbove[candidate] != weights.TipsAbove[current])
			{
				return weights.TipsAbove[candidate] > weights.TipsAbove[current];
			}

			if (graph.TimeOf(candidate) != graph.TimeOf(current))
			{
				return graph.TimeOf(candidate) > graph.TimeOf(current);
			}

			return candidate < current;
		}

		// Children before parents, so that each commit's weight is built from its children's.
		PathWeights WeighPaths(const AncestryGraph& graph, const PathTree& tree)
		{
			PathWeights weights;
			weights.TipsAbove.assign(graph.Size(), 0);
			weights.AuthorsAbove.assign(graph.Size(), 0);
			weights.HeaviestChild.assign(graph.Size(), AncestryGraph::kNoCommit);

			AuthorSetPool pool;
			std::vector<std::uint32_t> authorSetOf(graph.Size(), kNoAuthorSet);

			for (std::size_t remaining = tree.ParentsFirst.size(); remaining > 0; --remaining)
			{
				const Index commit = tree.ParentsFirst[remaining - 1];
				const std::span<const Index> children = PathChildrenOf(tree, commit);
				std::size_t tipsAbove = tree.IsTip[commit] ? 1 : 0;
				Index heaviest = AncestryGraph::kNoCommit;

				for (const Index child : children)
				{
					tipsAbove += weights.TipsAbove[child];

					if (heaviest == AncestryGraph::kNoCommit || IsHeavier(graph, weights, child, heaviest))
					{
						heaviest = child;
					}
				}

				for (const Index child : children)
				{
					if (child != heaviest)
					{
						pool.Release(authorSetOf[child]);
					}
				}

				const std::uint32_t authorSet = heaviest == AncestryGraph::kNoCommit ? pool.Take() : authorSetOf[heaviest];
				authorSetOf[commit] = authorSet;
				weights.AuthorsAbove[commit] = pool.Add(authorSet, graph.AuthorOf(commit));
				weights.TipsAbove[commit] = tipsAbove;
				weights.HeaviestChild[commit] = heaviest;
			}

			return weights;
		}

		struct BasePins
		{
			std::vector<bool> IsBaseTip;
			std::vector<std::uint32_t> PinnedBy; // per commit: the first listed base whose first-parent chain holds it, or kNoBase
		};

		// A base's walk stops at a commit an earlier base pinned: from there down, the chain is that base's too.
		BasePins PinBaseChains(const AncestryGraph& graph, std::span<const Index> baseTips)
		{
			BasePins pins;
			pins.IsBaseTip.assign(graph.Size(), false);
			pins.PinnedBy.assign(graph.Size(), kNoBase);

			for (std::uint32_t baseIndex = 0; baseIndex < baseTips.size(); ++baseIndex)
			{
				const Index baseTip = baseTips[baseIndex];
				Index commit = IsInGraph(graph, baseTip) ? baseTip : AncestryGraph::kNoCommit;

				if (commit != AncestryGraph::kNoCommit)
				{
					pins.IsBaseTip[commit] = true;
				}

				while (commit != AncestryGraph::kNoCommit && pins.PinnedBy[commit] == kNoBase)
				{
					pins.PinnedBy[commit] = baseIndex;
					commit = FirstParentOf(graph, commit);
				}
			}

			return pins;
		}

		Index FirstListedPinnedChild(const BasePins& pins, std::span<const Index> children)
		{
			Index pinnedChild = AncestryGraph::kNoCommit;
			std::uint32_t pinningBase = kNoBase;

			for (const Index child : children)
			{
				if (pins.PinnedBy[child] < pinningBase)
				{
					pinningBase = pins.PinnedBy[child];
					pinnedChild = child;
				}
			}

			return pinnedChild;
		}

		struct SplitPlan
		{
			PathTree Tree;
			PathWeights Weights;
			BasePins Pins;
		};

		// The distinct authors of a line from its bottom up to the commit being decided, counted no further
		// than the stack rule needs.
		struct AuthorsSoFar
		{
			std::array<std::uint32_t, kAuthorsBelowAStack> Seen{};
			std::size_t Count = 0;
		};

		bool HasSeen(const AuthorsSoFar& authors, std::uint32_t author)
		{
			for (std::size_t seenIndex = 0; seenIndex < authors.Count; ++seenIndex)
			{
				if (authors.Seen[seenIndex] == author)
				{
					return true;
				}
			}

			return false;
		}

		void CountAuthor(AuthorsSoFar& authors, std::uint32_t author)
		{
			if (authors.Count < authors.Seen.size() && !HasSeen(authors, author))
			{
				authors.Seen[authors.Count] = author;
				++authors.Count;
			}
		}

		// The child that keeps the commit's line, or kNoCommit when every child starts a line of its own.
		// A pinned child is never cut by the stack rule: a base's first-parent chain is always the continuation.
		Index ContinuationOf(const SplitPlan& plan, Index commit, const AuthorsSoFar& lineAuthors)
		{
			if (plan.Pins.IsBaseTip[commit])
			{
				return AncestryGraph::kNoCommit;
			}

			const Index pinnedChild = FirstListedPinnedChild(plan.Pins, PathChildrenOf(plan.Tree, commit));

			if (pinnedChild != AncestryGraph::kNoCommit)
			{
				return pinnedChild;
			}

			const Index heaviest = plan.Weights.HeaviestChild[commit];

			if (heaviest == AncestryGraph::kNoCommit)
			{
				return AncestryGraph::kNoCommit;
			}

			const bool lineBelowIsBusy = lineAuthors.Count >= kAuthorsBelowAStack;
			const bool workAboveIsOnePersons = plan.Weights.AuthorsAbove[heaviest] < kAuthorsAboveAStack;
			const bool workAboveIsAStack = plan.Tree.IsTip[commit] && lineBelowIsBusy && workAboveIsOnePersons;
			return workAboveIsAStack ? AncestryGraph::kNoCommit : heaviest;
		}

		void StartLine(LineSplit& split, std::vector<AuthorsSoFar>& lineAuthors, Index bottom, Index join)
		{
			const auto line = static_cast<std::uint32_t>(split.Lines.size());
			split.Lines.push_back(SplitLine{.Bottom = bottom, .Top = bottom, .Join = join});
			lineAuthors.emplace_back();
			split.LineOf[bottom] = line;
			split.PositionOf[bottom] = 0;
		}

		void ExtendLine(LineSplit& split, std::uint32_t line, Index below, Index above)
		{
			split.Lines[line].Top = above;
			split.LineOf[above] = line;
			split.PositionOf[above] = split.PositionOf[below] + 1;
		}
	}

	LineSplit SplitLines(const AncestryGraph& graph, std::span<const AncestryGraph::Index> tips,
						 std::span<const AncestryGraph::Index> baseTips)
	{
		SplitPlan plan;
		plan.Tree = BuildPathTree(graph, tips);
		plan.Weights = WeighPaths(graph, plan.Tree);
		plan.Pins = PinBaseChains(graph, baseTips);

		LineSplit split;
		split.LineOf.assign(graph.Size(), kNoLine);
		split.PositionOf.assign(graph.Size(), 0);
		std::vector<AuthorsSoFar> lineAuthors;

		for (const Index commit : plan.Tree.ParentsFirst)
		{
			if (FirstParentOf(graph, commit) == AncestryGraph::kNoCommit)
			{
				StartLine(split, lineAuthors, commit, AncestryGraph::kNoCommit);
			}

			const std::uint32_t line = split.LineOf[commit];
			CountAuthor(lineAuthors[line], graph.AuthorOf(commit));
			const Index continuation = ContinuationOf(plan, commit, lineAuthors[line]);

			for (const Index child : PathChildrenOf(plan.Tree, commit))
			{
				if (child == continuation)
				{
					ExtendLine(split, line, commit, child);
				}
				else
				{
					StartLine(split, lineAuthors, child, commit);
				}
			}
		}

		return split;
	}
}
