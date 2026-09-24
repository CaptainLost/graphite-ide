#pragma once

#include <chrono>
#include <string>

namespace Graphite {

	// How a wall-clock time is written: the time alone, the time with the month and day for a
	// row where the day changes, the whole date, or the whole date and the day of the week.
	enum class ClockStyle
	{
		Time,
		MonthDayTime,
		DateTime,
		DateTimeWeekday
	};

	// "18:52:33", "09-23 18:52:33", "2026-09-23 18:52:33", "2026-09-23 18:52:33 (Wednesday)".
	// The time is already on the reader's clock; this only writes it.
	std::string FormatLocalTime(std::chrono::local_seconds time, ClockStyle style);
}
