#pragma once

#include "Core/Git/Oid.h"
#include "Core/Git/Signature.h"

#include <string>
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
	};
}
