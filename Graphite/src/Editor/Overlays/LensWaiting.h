#pragma once

#include "Language/Api/LanguageService.h"

#include <string>
#include <string_view>

namespace Graphite {

	inline constexpr std::string_view kLensCountUnavailable = "references unavailable";

	// What a reference lens says while it has no count: what it waits for, or why no count will come. Empty where
	// no server is meant to count, so the row stays blank.
	std::string LensWaitingText(const ServerStatus& status);
}
