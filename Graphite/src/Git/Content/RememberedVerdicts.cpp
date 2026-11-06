#include "Git/Content/RememberedVerdicts.h"

#include "Core/Git/Lines/LineQuestions.h"

namespace Graphite {

	namespace {

		bool Holds(const Core::AncestryGraph& graph, std::span<const std::uint32_t> generations, Core::AncestryGraph::Index lineTip,
				   const Core::Oid& commit)
		{
			const Core::AncestryGraph::Index held = graph.Find(commit);

			if (held == Core::AncestryGraph::kNoCommit)
			{
				return false;
			}

			const std::vector<bool> holding =
				Core::TipsHolding(graph, generations, held, std::span<const Core::AncestryGraph::Index>{&lineTip, 1});
			return holding.front();
		}

		bool HoldsEveryCopy(const Core::AncestryGraph& graph, std::span<const std::uint32_t> generations,
							Core::AncestryGraph::Index lineTip, const std::vector<Core::PatchTwin>& twins)
		{
			for (const Core::PatchTwin& twin : twins)
			{
				if (!Holds(graph, generations, lineTip, twin.Twin))
				{
					return false;
				}
			}

			return true;
		}

		bool StillHolds(const Core::AncestryGraph& graph, std::span<const std::uint32_t> generations, const RememberedVerdict& remembered)
		{
			const Core::AncestryGraph::Index lineTip = graph.Find(remembered.Key.LineTip);

			if (lineTip == Core::AncestryGraph::kNoCommit)
			{
				return false;
			}

			const Core::ContentVerdict& verdict = remembered.Verdict;

			if (verdict.Kind == Core::ContentKind::Squash)
			{
				return verdict.Landing.has_value() && Holds(graph, generations, lineTip, *verdict.Landing);
			}

			return !verdict.Twins.empty() && HoldsEveryCopy(graph, generations, lineTip, verdict.Twins);
		}
	}

	std::vector<RememberedVerdict> VerdictsStillHeld(const Core::AncestryGraph& graph, std::span<const std::uint32_t> generations,
													 const std::vector<RememberedVerdict>& remembered)
	{
		std::vector<RememberedVerdict> held;

		for (const RememberedVerdict& candidate : remembered)
		{
			if (StillHolds(graph, generations, candidate))
			{
				held.push_back(candidate);
			}
		}

		return held;
	}
}
