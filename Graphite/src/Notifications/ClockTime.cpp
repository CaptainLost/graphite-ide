#include "Notifications/ClockTime.h"

#include "Core/Time/TimeZone.h"

#include <format>

namespace Graphite {

	std::string FormatClockTime(std::chrono::system_clock::time_point moment, const std::chrono::time_zone* zone)
	{
		const std::chrono::sys_time<std::chrono::minutes> localMinute =
			std::chrono::floor<std::chrono::minutes>(moment) +
			Core::OffsetAt(zone, std::chrono::floor<std::chrono::seconds>(moment));

		return std::format("{:%H:%M}", localMinute);
	}
}
