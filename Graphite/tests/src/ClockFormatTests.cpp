#include <doctest/doctest.h>

#include "Sessions/ClockFormat.h"

#include <chrono>

using namespace Graphite;

namespace {

	std::chrono::local_seconds At(int year, unsigned month, unsigned day, int hours, int minutes, int seconds)
	{
		const std::chrono::local_days date{std::chrono::year{year} / std::chrono::month{month} / std::chrono::day{day}};
		return date + std::chrono::hours{hours} + std::chrono::minutes{minutes} + std::chrono::seconds{seconds};
	}
}

TEST_CASE("FormatLocalTime writes each style with zero-padded fields")
{
	const auto time = At(2026, 9, 3, 8, 5, 7);

	CHECK(FormatLocalTime(time, ClockStyle::Time) == "08:05:07");
	CHECK(FormatLocalTime(time, ClockStyle::MonthDayTime) == "09-03 08:05:07");
	CHECK(FormatLocalTime(time, ClockStyle::DateTime) == "2026-09-03 08:05:07");
	CHECK(FormatLocalTime(time, ClockStyle::DateTimeWeekday) == "2026-09-03 08:05:07 (Thursday)");
}

TEST_CASE("FormatLocalTime writes the date alone, zero-padded, with no time")
{
	CHECK(FormatLocalTime(At(2026, 9, 3, 8, 5, 7), ClockStyle::Date) == "2026-09-03");
	CHECK(FormatLocalTime(At(2026, 12, 31, 23, 59, 59), ClockStyle::Date) == "2026-12-31");
}

TEST_CASE("FormatLocalTime names the weekday in English, Sunday included")
{
	CHECK(FormatLocalTime(At(2026, 9, 23, 18, 52, 33), ClockStyle::DateTimeWeekday) ==
		  "2026-09-23 18:52:33 (Wednesday)");
	CHECK(FormatLocalTime(At(2026, 9, 27, 0, 0, 0), ClockStyle::DateTimeWeekday) == "2026-09-27 00:00:00 (Sunday)");
	CHECK(FormatLocalTime(At(2026, 9, 26, 23, 59, 59), ClockStyle::DateTimeWeekday) ==
		  "2026-09-26 23:59:59 (Saturday)");
}
