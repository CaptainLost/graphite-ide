#pragma once

#include "Git/Work/WorkBoard.h"
#include "Review/ReviewFiles.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	// Chip texts in order: distance, uncommitted, twins, check, lock, detached, operation.
	std::vector<std::string> CardChips(const WorkCard& card, std::string_view trunkName);

	// "Reviewed 3/10"; "Reviewed …" while the count is unknown, never a zero.
	std::string ReviewProgressText(const std::optional<ViewedCount>& viewed);
}
