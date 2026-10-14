#pragma once

#include <chrono>
#include <string>

namespace Graphite {

	enum class ClockStyle
	{
		Time,
		MonthDayTime,
		DateTime,
		DateTimeWeekday,
		Date,
		MonthDayHoursMinutes
	};

	std::string FormatLocalTime(std::chrono::local_seconds time, ClockStyle style);
}
