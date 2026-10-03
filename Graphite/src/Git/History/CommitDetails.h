#pragma once

#include "Core/Git/Commit.h"
#include "Core/Git/Diff.h"

#include <chrono>
#include <cstddef>
#include <string>

namespace Graphite {

	// How much a commit changed, as the one line a reader wants above the file list.
	// Core::Diff carries no such summary of its own, so this is counted from the hunks
	// every time it is asked for — cheap over a diff the model already holds, and nothing
	// to keep in step with the diff it describes.
	struct DiffStats
	{
		std::size_t Files = 0;
		std::size_t Added = 0;
		std::size_t Removed = 0;
	};

	// Context lines are not changes and are not counted. A binary file has no hunks at all,
	// so it raises Files and neither of the other two — which is the whole truth about it:
	// something changed, and how much cannot be said in lines.
	DiffStats CountChanges(const Core::Diff& diff);

	// "2026-08-29 14:12", the instant shifted by `offset` before being named. The offset is
	// taken rather than read from the system, so this stays a pure function of its
	// arguments — the same rule RelativeAge follows for the same reason.
	//
	// Not the author's own offset: Core::Signature drops that on purpose (see its
	// definition), so the only offset available is the reader's, which is also the one that
	// makes a date comparable with everything else on their screen.
	//
	// Seconds are dropped, not rounded: this sits beside a relative age, and a minute is
	// already finer than that line's own precision.
	std::string FormattedTimestamp(std::chrono::sys_seconds when, std::chrono::minutes offset);

	// Whether the committer is worth naming beside the author. Compared by name and email
	// and not by date: a rebase or an amend leaves the same person with a later committer
	// date, and a line repeating their name to report that tells the reader nothing they
	// asked about.
	bool ShowsCommitter(const Core::CommitSummary& commit);
}
