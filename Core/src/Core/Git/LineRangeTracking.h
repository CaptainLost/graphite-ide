#pragma once

#include "Core/Git/Diff.h"
#include "Core/Git/Requests.h"
#include "Core/Git/TextDiff.h"

#include <optional>

namespace Core {

	// A commit that adds or deletes the file touches every range in it. Otherwise a hunk with
	// per-line detail touches the range only by changing a line inside it, not by carrying it
	// as context; one without counts by overlap alone.
	bool TouchesRange(const FileDiff& file, const OneBasedLineRange& range);

	// Empty when the file did not exist in the parent, which is where a line-range history
	// ends.
	std::optional<OneBasedLineRange> MapRangeToParent(const FileDiff& file, const OneBasedLineRange& range);

	// One-based on both sides. The same rule as TraceLineToOld, reading each change from a hunk's
	// lines: a maximal run of Removed and Added lines between Context lines, its old lines the
	// Removed ones and its new lines the Added ones, in order. A file that is new in this commit
	// has every line Added.
	TracedLine TraceLineToParent(const FileDiff& file, std::size_t line);
}
