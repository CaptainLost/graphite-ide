#pragma once

#include "Git/Compare/VsLabel.h"

#include <optional>
#include <string>

namespace Graphite {

	// The sentence above the history list when the shown reference needs one: merged, no commits of its own, an old copy
	// of another remote's default, history cut off, or no common history.
	std::optional<std::string> HistoryBannerFor(const VsInput& input);
}
