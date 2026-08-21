#pragma once

#include "Core/Git/Commit.h"

#include <string>

namespace Core {

	// What narrows a log to the commits a reader is looking for. One text, matched every
	// way a commit can be named, rather than a field per question: a reader who has a hash
	// in the clipboard and a reader who remembers half a summary both type into the same
	// box, and asking them to first say which kind of thing they typed is asking them to
	// answer a question the filter can work out for itself.
	struct CommitFilter
	{
		// Surrounding whitespace is not part of it, and a text of nothing else accepts
		// every commit — a filter box that has been cleared back to spaces means the
		// reader is done filtering, not that they want an empty list.
		std::string Text;
	};

	// Case is ignored throughout. The text is matched against the summary, the body, and
	// the author's name and email as a substring; against the id it is matched as a
	// prefix, and only when it is hexadecimal throughout — that is how git itself resolves
	// an abbreviated hash, and a substring search over forty digits would match nearly
	// anything short enough to type.
	//
	// A free function over the two structures rather than a method on either: the filter
	// knows nothing about commits, a commit knows nothing about being searched, and the
	// walk that uses this needs neither of them to grow a dependency on the other.
	bool Matches(const CommitFilter& filter, const CommitSummary& commit);
}
