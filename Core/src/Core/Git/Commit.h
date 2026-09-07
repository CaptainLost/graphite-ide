#pragma once

#include "Core/Git/Oid.h"
#include "Core/Git/Signature.h"

#include <string>
#include <unordered_set>
#include <vector>

namespace Core {

	struct CommitSummary
	{
		Oid Id;

		// In git's own order: the first parent is the branch that was checked out when a
		// merge was made. The graph relies on that order to keep a branch on one lane.
		std::vector<Oid> Parents;

		Signature Author;
		Signature Committer;

		// The first line, and everything after the blank line that follows it. A commit
		// whose message is one line has an empty Body.
		std::string Summary;
		std::string Body;
	};

	// A page of history, and whether it is all of it. The walk stops early for four
	// reasons — the requested page size, the walk-length ceiling, a diff that could not be
	// read, and a line range that cannot be carried further back — and a caller that
	// cannot tell a short page from a finished one would present "no more history" where
	// the truth is "we stopped looking".
	struct LogPage
	{
		std::vector<CommitSummary> Commits;
		bool Complete = true;

		// The commits on the walked ref that its upstream does not have — what a push
		// would send. Measured against that one upstream and nothing else: a commit
		// already sitting on some other remote branch is still listed here, because the
		// question this answers is the one the branch's own ahead count answers, and two
		// parts of a panel disagreeing about the same number reads as a bug.
		//
		// Not a flag on CommitSummary: whether a commit has been pushed is a property of
		// its relation to the ref being walked, not a fact recorded in the commit, and the
		// same commit is unpushed in one branch's log and pushed in another's.
		std::unordered_set<Oid> Unpushed;

		// Whether the upstream was found at all. False for a branch with none, for a
		// detached HEAD, and for the log of a remote-tracking ref, which has no upstream of
		// its own. Empty and known means "nothing to push", which is a different answer
		// from "there is nothing to compare against", so a caller that skips this check
		// cannot tell the two apart — the same rule Branch::DistanceKnown states.
		bool UpstreamKnown = false;
	};
}
