#include "Sessions/ClockFormat.h"

#include "Core/Base/Base.h"

#include <format>

namespace Graphite {

	std::string FormatLocalTime(std::chrono::local_seconds time, ClockStyle style)
	{
		// WARNING: The weekday names are English whatever the reader's locale, like the rest of the interface.
		static constexpr const char* kWeekdays[] = {"Sunday", "Monday", "Tuesday", "Wednesday",
													"Thursday", "Friday", "Saturday"};

		const std::chrono::local_days localDay = std::chrono::floor<std::chrono::days>(time);
		const std::chrono::year_month_day date{localDay};
		const std::chrono::hh_mm_ss<std::chrono::seconds> timeOfDay{time - localDay};

		const std::string hoursMinutesSeconds = std::format("{:02}:{:02}:{:02}", timeOfDay.hours().count(),
															timeOfDay.minutes().count(), timeOfDay.seconds().count());
		const int year = static_cast<int>(date.year());
		const unsigned month = static_cast<unsigned>(date.month());
		const unsigned dayOfMonth = static_cast<unsigned>(date.day());

		switch (style)
		{
		case ClockStyle::Time:
			return hoursMinutesSeconds;
		case ClockStyle::MonthDayTime:
			return std::format("{:02}-{:02} {}", month, dayOfMonth, hoursMinutesSeconds);
		case ClockStyle::DateTime:
			return std::format("{:04}-{:02}-{:02} {}", year, month, dayOfMonth, hoursMinutesSeconds);
		case ClockStyle::DateTimeWeekday:
			return std::format("{:04}-{:02}-{:02} {} ({})", year, month, dayOfMonth, hoursMinutesSeconds,
							   kWeekdays[std::chrono::weekday{localDay}.c_encoding()]);
		case ClockStyle::Date:
			return std::format("{:04}-{:02}-{:02}", year, month, dayOfMonth);
		case ClockStyle::MonthDayHoursMinutes:
			return std::format("{:02}-{:02} {:02}:{:02}", month, dayOfMonth, timeOfDay.hours().count(),
							   timeOfDay.minutes().count());
		}

		return hoursMinutesSeconds;
	}
}
