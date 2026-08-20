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
}
