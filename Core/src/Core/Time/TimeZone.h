#pragma once

#include <chrono>

namespace Core {

	// Null on a Windows install without ICU's time zone data, where chrono's lookup throws. Each lookup is an ICU call
	// and a search of every zone, so a caller asking every frame keeps what this returns.
	const std::chrono::time_zone* LocalTimeZone();

	// The zone's offset at that moment, which differs from today's across a daylight-saving change. Zero, so a time
	// reads as UTC, when there is no zone or ICU cannot give the zone's offset at that moment. Each span between two of
	// the zone's transitions is read from ICU once a thread, so a list may ask once a row every frame.
	std::chrono::minutes OffsetAt(const std::chrono::time_zone* zone, std::chrono::sys_seconds instant);
}
