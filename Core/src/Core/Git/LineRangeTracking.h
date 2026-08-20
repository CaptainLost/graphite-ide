#pragma once

#include "Core/Git/Diff.h"
#include "Core/Git/Requests.h"

#include <optional>

namespace Core {

	// Whether any hunk of this file's diff overlaps the range. A commit that adds or
	// deletes the file counts as touching every range in it.
	bool TouchesRange(const FileDiff& file, const LineRange& range);

	// The lines this range came from, one commit earlier. Empty when the file did not
	// exist in the parent, which is where a line-range history ends.
	std::optional<LineRange> MapRangeToParent(const FileDiff& file, const LineRange& range);
}
