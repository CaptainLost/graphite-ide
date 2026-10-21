#pragma once

#include "Language/Api/LanguageService.h"

#include <string>
#include <string_view>

namespace Graphite {

	inline constexpr std::string_view kLensCountUnavailable = "references unavailable";

	std::string LensWaitingText(const ServerStatus& status);
}
