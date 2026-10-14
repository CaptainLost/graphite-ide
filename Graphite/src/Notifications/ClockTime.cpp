#include "Notifications/ClockTime.h"

#include <exception>
#include <format>

namespace Graphite {

	const std::chrono::time_zone* LocalTimeZone()
	{
		try
		{
			return std::chrono::current_zone();
		}
		catch (const std::exception&)
		{
			return nullptr;
		}
	}

	std::string FormatClockTime(std::chrono::system_clock::time_point moment, const std::chrono::time_zone* zone)
	{
		const std::chrono::sys_time<std::chrono::minutes> utcMinute = std::chrono::floor<std::chrono::minutes>(moment);
		if (zone == nullptr)
		{
			return std::format("{:%H:%M}", utcMinute);
		}

		// WARNING: A zone that was found still asks ICU for its offset at each moment, and that can throw too.
		try
		{
			const std::chrono::zoned_time<std::chrono::system_clock::duration> local{zone, moment};
			return std::format("{:%H:%M}", local);
		}
		catch (const std::exception&)
		{
			return std::format("{:%H:%M}", utcMinute);
		}
	}
}
