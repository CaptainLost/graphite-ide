#include "Sessions/ClockFormat.h"

#include "Core/Base/Base.h"

#include <format>

namespace Graphite {

	std::string FormatLocalTime(std::chrono::local_seconds time, ClockStyle style)
	{
		// English names whatever the reader's locale, like the rest of the interface.
		static constexpr const char* kWeekdays[] = {"Sunday", "Monday", "Tuesday", "Wednesday",
													"Thursday", "Friday", "Saturday"};

		const auto day = std::chrono::floor<std::chrono::days>(time);
		const std::chrono::year_month_day date{day};
		const std::chrono::hh_mm_ss clock{time - day};

		const std::string hms = std::format("{:02}:{:02}:{:02}", clock.hours().count(), clock.minutes().count(),
											clock.seconds().count());
		const int year = static_cast<int>(date.year());
		const unsigned month = static_cast<unsigned>(date.month());
		const unsigned dayOfMonth = static_cast<unsigned>(date.day());

		switch (style)
		{
		case ClockStyle::Time:
			return hms;
		case ClockStyle::MonthDayTime:
			return std::format("{:02}-{:02} {}", month, dayOfMonth, hms);
		case ClockStyle::DateTime:
			return std::format("{:04}-{:02}-{:02} {}", year, month, dayOfMonth, hms);
		case ClockStyle::DateTimeWeekday:
			return std::format("{:04}-{:02}-{:02} {} ({})", year, month, dayOfMonth, hms,
							   kWeekdays[std::chrono::weekday{day}.c_encoding()]);
		case ClockStyle::Date:
			return std::format("{:04}-{:02}-{:02}", year, month, dayOfMonth);
		case ClockStyle::MonthDayHoursMinutes:
			return std::format("{:02}-{:02} {:02}:{:02}", month, dayOfMonth, clock.hours().count(),
							   clock.minutes().count());
		}

		return hms;
	}
}
