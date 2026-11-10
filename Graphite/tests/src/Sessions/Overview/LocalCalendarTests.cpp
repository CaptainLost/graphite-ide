#include <doctest/doctest.h>

#include "Sessions/Overview/LocalCalendar.h"

using namespace Graphite;

namespace {
	UtcHour Hour(int year, unsigned month, unsigned day, int hour)
	{
		return std::chrono::sys_days{std::chrono::year{year} / month / day} + std::chrono::hours{hour};
	}

	LocalHour Local(int year, unsigned month, unsigned day, int hour, int weekday)
	{
		return {std::chrono::year{year} / month / day, hour, weekday};
	}
}

TEST_CASE("LocalCalendar puts UTC hours on Warsaw days, hours and weekdays across the autumn switch")
{
	LocalCalendar calendar{std::chrono::locate_zone("Europe/Warsaw")};

	CHECK(calendar.At(Hour(2026, 10, 24, 21)) == Local(2026, 10, 24, 23, 5));
	CHECK(calendar.At(Hour(2026, 10, 24, 22)) == Local(2026, 10, 25, 0, 6));
	CHECK(calendar.At(Hour(2026, 10, 25, 1)) == Local(2026, 10, 25, 2, 6));
	CHECK(calendar.At(Hour(2026, 10, 25, 23)) == Local(2026, 10, 26, 0, 0));
}

TEST_CASE("LocalCalendar reads the local day of an ISO timestamp")
{
	LocalCalendar calendar{std::chrono::locate_zone("Europe/Warsaw")};

	CHECK(calendar.DayOf("2026-11-10T23:30:00.000Z") == std::chrono::year{2026} / 11 / 11);
	CHECK_FALSE(calendar.DayOf("garbage").has_value());
}

TEST_CASE("LocalCalendar without a zone is UTC")
{
	LocalCalendar calendar{nullptr};

	CHECK(calendar.At(Hour(2026, 10, 24, 22)) == Local(2026, 10, 24, 22, 5));
	CHECK(calendar.DayOf("2026-11-10T23:30:00.000Z") == std::chrono::year{2026} / 11 / 10);
}
