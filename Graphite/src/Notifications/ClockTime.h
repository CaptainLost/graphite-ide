#pragma once

#include <chrono>
#include <string>

namespace Graphite {

	// Null on a Windows install without ICU's time zone data, where chrono's lookup throws.
	// Looked up once: each lookup is an ICU call and a search of every zone.
	const std::chrono::time_zone* LocalTimeZone();

	std::string FormatClockTime(std::chrono::system_clock::time_point moment, const std::chrono::time_zone* zone);
}
