#pragma once

#include "Git/Content/ContentPort.h"
#include "Git/Content/ContentStore.h"

#include "Core/Base/Base.h"
#include "Core/Git/Content/ContentCheck.h"
#include "Core/Git/Lines/AncestryGraph.h"
#include "Core/Git/Oid.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Graphite {

	// A branch found merged by its contents against an earlier line tip, asked about with the line tip it has now.
	struct RememberedVerdict
	{
		ContentKey Key; // the branch tip and the line tip it has now
		Core::ContentVerdict Verdict;
	};

	// Those whose line tip, in the graph, still holds what was found: the squash's landing, or every copy of the rebase.
	// A squash whose landing was not found is never held. The line's later edits to the same lines would make a new check
	// say no, so a held verdict counts unchecked. One walk of the graph per distinct line tip answers them all.
	std::vector<RememberedVerdict> VerdictsStillHeld(const Core::AncestryGraph& graph, const std::vector<RememberedVerdict>& remembered);

	// A verdict, not merged or unknown, a check gave for a branch tip against an earlier tip of its line, asked about with
	// the line tip it has now.
	struct CarryAsk
	{
		ContentKey Key;           // the branch tip and the line tip it has now
		Core::Oid EarlierLineTip; // the line tip the verdict was found against
		Core::ContentVerdict Verdict;
	};

	struct CarriedVerdicts
	{
		std::vector<CarryAsk> Carried; // hold for the line tip they are asked with, unchecked
		std::vector<CarryAsk> Refused; // the port no longer opens the repository they were asked of: asked again
	};

	// Those a check of the new pair would answer as the earlier one did: the earlier line tip is an ancestor of the new
	// one, the new tip holds none of the branch's own commits against the earlier one (so the merge bases are the same),
	// and the trees from one to the other differ at none of the paths the branch changed since its fork with the earlier
	// tip, so merging the branch and walking its patches meet the same files. The rest go back to the checks: an earlier
	// tip that is not an ancestor, a move that took some of the branch in by history, a path the line changed, or an object
	// the diffs need missing.
	//
	// Reads trees only, so a blobless clone answers: one diff of the line's two trees per line move, and per verdict one
	// diff from the branch's fork, which the graph gives, to its tip, kept to the paths that move changed.
	// WARNING: The line's trees are compared, not its commits: a change the line made and undid within one move, a
	// cherry-pick of the branch's then its revert, is not seen until the branch or the line moves again.
	CarriedVerdicts CarryVerdicts(const Core::AncestryGraph& graph, std::span<const std::uint32_t> generations, ContentPort& port,
								  std::uint64_t repository, const std::vector<CarryAsk>& asks);

	// What the one job that resolves a moved line's earlier verdicts finds.
	struct ResolvedVerdicts
	{
		std::vector<RememberedVerdict> Held; // merged, and the line still holds what was found
		CarriedVerdicts Carry;               // not merged or unknown, and the line's move left the branch's paths alone
	};

	// Both at once. Without a graph, or its generations, nothing is held or carried: every pair goes to the checks.
	ResolvedVerdicts ResolveVerdicts(const Core::Ref<const Core::AncestryGraph>& graph,
									 const Core::Ref<const std::vector<std::uint32_t>>& generations, ContentPort& port,
									 std::uint64_t repository, const std::vector<RememberedVerdict>& remembered,
									 const std::vector<CarryAsk>& asks);
}
