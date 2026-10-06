#include "Notifications/ClockTime.h"

#include <format>

namespace Graphite {

	std::string FormatClockTime(std::chrono::system_clock::time_point moment, const std::chrono::time_zone& zone)
	{
		const std::chrono::zoned_time<std::chrono::system_clock::duration> local{&zone, moment};
		return std::format("{:%H:%M}", local);
	}
}
