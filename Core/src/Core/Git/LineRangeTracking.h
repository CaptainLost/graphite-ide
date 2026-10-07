#pragma once

#include "Core/Git/Diff.h"
#include "Core/Git/Requests.h"

#include <optional>

namespace Core {

	// A commit that adds or deletes the file touches every range in it. Otherwise a hunk with
	// per-line detail touches the range only by changing a line inside it, not by carrying it
	// as context; one without counts by overlap alone.
	bool TouchesRange(const FileDiff& file, const OneBasedLineRange& range);

	// Empty when the file did not exist in the parent, which is where a line-range history
	// ends.
	std::optional<OneBasedLineRange> MapRangeToParent(const FileDiff& file, const OneBasedLineRange& range);
}
