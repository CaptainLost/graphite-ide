#pragma once

#include "Git/Content/ContentStore.h"

#include "Core/Git/Content/ContentCheck.h"
#include "Core/Git/Lines/AncestryGraph.h"

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
	// say no, so a held verdict counts unchecked.
	std::vector<RememberedVerdict> VerdictsStillHeld(const Core::AncestryGraph& graph, std::span<const std::uint32_t> generations,
													 const std::vector<RememberedVerdict>& remembered);
}
