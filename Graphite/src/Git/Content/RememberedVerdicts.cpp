#include "Git/Content/RememberedVerdicts.h"

#include "Core/Git/Lines/LineQuestions.h"

#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>

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

	namespace {

		enum class CarryOutcome
		{
			Carried,
			CheckedAgain,
			Refused,
		};

		struct LineMove
		{
			Core::Oid From;
			Core::Oid To;

			friend auto operator<=>(const LineMove&, const LineMove&) = default;
		};

		// What one move of a line leaves its verdicts: none carries, or those whose branch changed none of its paths do.
		struct MoveAnswer
		{
			CarryOutcome Outcome = CarryOutcome::CheckedAgain; // Carried: the verdicts may carry
			std::vector<std::string> Paths;                    // where the line's trees differ
		};

		bool IsRefused(const Core::Error& error)
		{
			return error.Code == Core::ErrorCode::Cancelled;
		}

		CarryOutcome OutcomeOfFailure(const Core::Error& error)
		{
			return IsRefused(error) ? CarryOutcome::Refused : CarryOutcome::CheckedAgain;
		}

		bool IsAncestor(const Core::AncestryGraph& graph, std::span<const std::uint32_t> generations, const Core::Oid& older,
						const Core::Oid& newer)
		{
			const Core::AncestryGraph::Index olderCommit = graph.Find(older);
			const Core::AncestryGraph::Index newerCommit = graph.Find(newer);

			if (olderCommit == Core::AncestryGraph::kNoCommit || newerCommit == Core::AncestryGraph::kNoCommit)
			{
				return false;
			}

			return Core::TipsHolding(graph, generations, olderCommit, std::span<const Core::AncestryGraph::Index>{&newerCommit, 1}).front();
		}

		MoveAnswer AnswerMove(const Core::AncestryGraph& graph, std::span<const std::uint32_t> generations, ContentPort& port,
							  std::uint64_t repository, const LineMove& move)
		{
			if (!IsAncestor(graph, generations, move.From, move.To))
			{
				return MoveAnswer{};
			}

			Core::Result<std::vector<std::string>> paths = port.PathsChangedBetween(repository, move.From, move.To);

			if (!paths.HasValue())
			{
				return MoveAnswer{.Outcome = OutcomeOfFailure(paths.GetError()), .Paths = {}};
			}

			return MoveAnswer{.Outcome = CarryOutcome::Carried, .Paths = std::move(paths).GetValue()};
		}

		const MoveAnswer& MoveAnswerOf(const Core::AncestryGraph& graph, std::span<const std::uint32_t> generations, ContentPort& port,
									   std::uint64_t repository, const LineMove& move, std::map<LineMove, MoveAnswer>& answers)
		{
			const std::map<LineMove, MoveAnswer>::const_iterator known = answers.find(move);

			if (known != answers.end())
			{
				return known->second;
			}

			return answers.emplace(move, AnswerMove(graph, generations, port, repository, move)).first->second;
		}

		struct CarryContext
		{
			const Core::AncestryGraph& Graph;
			std::span<const std::uint32_t> Generations;
			ContentPort& Port;
			std::uint64_t Repository = 0;
			std::map<LineMove, MoveAnswer> Moves;
			std::map<Core::Oid, Reach> ReachByLineTip;
		};

		CarryOutcome AnswerAsk(CarryContext& context, const CarryAsk& ask)
		{
			const LineMove move{.From = ask.EarlierLineTip, .To = ask.Key.LineTip};
			const MoveAnswer& moved = MoveAnswerOf(context.Graph, context.Generations, context.Port, context.Repository, move, context.Moves);

			if (moved.Outcome != CarryOutcome::Carried)
			{
				return moved.Outcome;
			}

			const Core::AncestryGraph::Index tip = context.Graph.Find(ask.Key.BranchTip);
			const Reach& reach = ReachOf(context.Graph, ask.EarlierLineTip, context.ReachByLineTip);
			const Reach& reachNow = ReachOf(context.Graph, ask.Key.LineTip, context.ReachByLineTip);

			if (tip == Core::AncestryGraph::kNoCommit || reach.empty() || reachNow.empty())
			{
				return CarryOutcome::CheckedAgain;
			}

			// WARNING: Asked before the paths. A move that takes some of the branch's own commits in by history moves its
			// merge bases, and the check of the new pair merges from those, however few paths the line's trees changed:
			// a merge keeping the line's side changes none.
			if (Core::HoldsOwnCommitOf(context.Graph, reach, reachNow, tip))
			{
				return CarryOutcome::CheckedAgain;
			}

			if (moved.Paths.empty())
			{
				return CarryOutcome::Carried;
			}

			const std::optional<Core::AncestryGraph::Index> fork = Core::ForkWith(context.Graph, context.Generations, reach, tip);

			if (!fork.has_value())
			{
				return CarryOutcome::CheckedAgain;
			}

			const Core::Result<bool> touched =
				context.Port.TreesDifferAt(context.Repository, context.Graph.IdOf(*fork), ask.Key.BranchTip, moved.Paths);

			if (!touched.HasValue())
			{
				return OutcomeOfFailure(touched.GetError());
			}

			return touched.GetValue() ? CarryOutcome::CheckedAgain : CarryOutcome::Carried;
		}
	}

	CarriedVerdicts CarryVerdicts(const Core::AncestryGraph& graph, std::span<const std::uint32_t> generations, ContentPort& port,
								  std::uint64_t repository, const std::vector<CarryAsk>& asks)
	{
		CarryContext context{.Graph = graph, .Generations = generations, .Port = port, .Repository = repository};
		CarriedVerdicts answers;

		for (const CarryAsk& ask : asks)
		{
			switch (AnswerAsk(context, ask))
			{
			case CarryOutcome::Carried:
				answers.Carried.push_back(ask);
				break;
			case CarryOutcome::Refused:
				answers.Refused.push_back(ask);
				break;
			case CarryOutcome::CheckedAgain:
				break;
			}
		}

		return answers;
	}

	ResolvedVerdicts ResolveVerdicts(const Core::Ref<const Core::AncestryGraph>& graph,
									 const Core::Ref<const std::vector<std::uint32_t>>& generations, ContentPort& port,
									 std::uint64_t repository, const std::vector<RememberedVerdict>& remembered,
									 const std::vector<CarryAsk>& asks)
	{
		if (graph == nullptr || generations == nullptr)
		{
			return ResolvedVerdicts{};
		}

		return ResolvedVerdicts{.Held = VerdictsStillHeld(*graph, remembered),
								.Carry = CarryVerdicts(*graph, *generations, port, repository, asks)};
	}
}
