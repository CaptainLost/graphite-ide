#pragma once

#include <chrono>

namespace Core {

	// Null on a Windows install without ICU's time zone data, where chrono's lookup throws. Each lookup is an ICU call
	// and a search of every zone, so a caller asking every frame keeps what this returns.
	const std::chrono::time_zone* LocalTimeZone();

	// Zero, so a time reads as UTC, when there is no zone or ICU cannot give the zone's offset at that moment.
	std::chrono::minutes OffsetAt(const std::chrono::time_zone* zone, std::chrono::sys_seconds instant);
}