#pragma once

#include "Core/Git/Oid.h"
#include "Core/Git/Signature.h"

#include <cstddef>
#include <string>
#include <unordered_set>
#include <vector>

namespace Core {

	struct CommitSummary
	{
		Oid Id;

		// In git's order: the first parent is the branch that was checked out when the merge
		// was made, and the graph relies on that to keep a branch on one lane.
		std::vector<Oid> Parents;

		Signature Author;
		Signature Committer;

		// Summary is the first line of the message, and Body everything after the blank line
		// that follows it, so a one-line message has an empty Body.
		std::string Summary;
		std::string Body;
	};

	struct LogPage
	{
		std::vector<CommitSummary> Commits;

		// False when the walk stopped before the history ended: at the page size, at the
		// walk-length ceiling, on an unreadable diff, or where a line range cannot be carried
		// further back.
		bool Complete = true;

		// The commits the walk visited, kept or not: the commits kept when nothing narrows the walk,
		// and one for a stash, which is shown without a walk.
		std::size_t Walked = 0;

		// Commits on the walked ref that its upstream lacks, measured against that upstream
		// alone so the set agrees with the branch's ahead count. Not a flag on CommitSummary,
		// because the same commit is unpushed in one branch's log and pushed in another's.
		std::unordered_set<Oid> Unpushed;

		// False with no upstream to compare against: a branch without one, a detached HEAD,
		// or a remote-tracking ref. Also false when MaxWalk ran out before the upstream was
		// reached. An empty Unpushed means "nothing to push" only when true.
		bool UpstreamKnown = false;
	};
}
