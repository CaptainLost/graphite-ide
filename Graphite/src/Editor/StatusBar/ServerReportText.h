#pragma once

#include "Language/Api/LanguageService.h"

#include <chrono>
#include <cstddef>
#include <string>

namespace Graphite {

	inline constexpr std::size_t kServerEventsShown = 8;

	// The lines the server's tooltip adds below its detail: what it works on, where it runs, how busy it is, how
	// long it has been in its state, and its last kServerEventsShown events at the local time of day, oldest first.
	std::string ServerReportText(const ServerReport& report, std::chrono::minutes localOffset);
}
