#pragma once

#include "Language/Api/LanguageService.h"

#include <chrono>
#include <cstddef>
#include <string>

namespace Graphite {

	inline constexpr std::size_t kServerEventsShown = 8;

	std::string ServerReportText(const ServerReport& report, std::chrono::minutes localOffset);
}
