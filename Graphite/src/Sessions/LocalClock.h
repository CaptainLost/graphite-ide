#pragma once

#include "Sessions/ClockFormat.h"

#include <chrono>
#include <optional>
#include <string>

namespace Graphite {

	class LocalClock
	{
	public:
		std::string HoursMinutes(const std::string& timestamp);

		std::string HoursMinutesSeconds(const std::string& timestamp);

		std::optional<std::chrono::local_seconds> LocalTime(const std::string& timestamp);

		std::string Format(const std::string& timestamp, ClockStyle style);

	private:
		std::chrono::minutes OffsetAt(std::chrono::sys_time<std::chrono::milliseconds> instant);

		const std::chrono::time_zone* m_Zone = nullptr;
		bool m_ZoneResolved = false;
		std::optional<std::chrono::sys_info> m_CachedZoneInfo;
	};
}
