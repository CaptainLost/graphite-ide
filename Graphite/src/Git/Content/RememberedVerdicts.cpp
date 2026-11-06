#include "Git/Content/RememberedVerdicts.h"

#include "Core/Git/Lines/LineQuestions.h"

#include <map>
#include <span>

namespace Graphite {

	namespace {

		// Per commit of the graph, whether the line tip reaches it; empty for a line tip the graph lacks.
		using Reach = std::vector<bool>;

		const Reach& ReachOf(const Core::AncestryGraph& graph, const Core::Oid& lineTip, std::map<Core::Oid, Reach>& reachByLineTip)
		{
			const std::map<Core::Oid, Reach>::const_iterator known = reachByLineTip.find(lineTip);

			if (known != reachByLineTip.end())
			{
				return known->second;
			}

			// WARNING: A reference into the map stays valid as later line tips are added.
			Reach& reach = reachByLineTip[lineTip];
			const Core::AncestryGraph::Index tip = graph.Find(lineTip);

			if (tip != Core::AncestryGraph::kNoCommit)
			{
				reach = Core::CommitsReachableFrom(graph, std::span<const Core::AncestryGraph::Index>{&tip, 1});
			}

			return reach;
		}

		bool Holds(const Core::AncestryGraph& graph, const Reach& reach, const Core::Oid& commit)
		{
			const Core::AncestryGraph::Index held = graph.Find(commit);

			return held != Core::AncestryGraph::kNoCommit && held < reach.size() && reach[held];
		}

		bool HoldsEveryCopy(const Core::AncestryGraph& graph, const Reach& reach, const std::vector<Core::PatchTwin>& twins)
		{
			for (const Core::PatchTwin& twin : twins)
			{
				if (!Holds(graph, reach, twin.Twin))
				{
					return false;
				}
			}

			return true;
		}

		bool StillHolds(const Core::AncestryGraph& graph, const Reach& reach, const Core::ContentVerdict& verdict)
		{
			if (verdict.Kind == Core::ContentKind::Squash)
			{
				return verdict.Landing.has_value() && Holds(graph, reach, *verdict.Landing);
			}

			return !verdict.Twins.empty() && HoldsEveryCopy(graph, reach, verdict.Twins);
		}
	}

	std::vector<RememberedVerdict> VerdictsStillHeld(const Core::AncestryGraph& graph, const std::vector<RememberedVerdict>& remembered)
	{
		std::map<Core::Oid, Reach> reachByLineTip;
		std::vector<RememberedVerdict> held;

		for (const RememberedVerdict& candidate : remembered)
		{
			const Reach& reach = ReachOf(graph, candidate.Key.LineTip, reachByLineTip);

			if (StillHolds(graph, reach, candidate.Verdict))
			{
				held.push_back(candidate);
			}
		}

		return held;
	}
}
