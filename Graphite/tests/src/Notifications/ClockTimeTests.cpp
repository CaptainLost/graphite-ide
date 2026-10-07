#include <doctest/doctest.h>

#include "Notifications/ClockTime.h"

#include <chrono>

namespace {
	using namespace std::chrono_literals;

	std::chrono::system_clock::time_point UtcOn6October(std::chrono::seconds sinceMidnight)
	{
		return std::chrono::sys_days{std::chrono::year{2026} / 10 / 6} + sinceMidnight;
	}
}

TEST_CASE("A notification's time reads as hours and minutes, seconds dropped")
{
	CHECK(Graphite::FormatClockTime(UtcOn6October(14h + 32min + 59s), std::chrono::locate_zone("UTC")) == "14:32");
}

TEST_CASE("An hour before ten keeps its leading zero")
{
	CHECK(Graphite::FormatClockTime(UtcOn6October(9h + 5min), std::chrono::locate_zone("UTC")) == "09:05");
}

TEST_CASE("Without time zone data a notification's time reads in UTC")
{
	const std::chrono::time_zone* const noTimeZoneData = nullptr;

	CHECK(Graphite::FormatClockTime(UtcOn6October(14h + 32min + 59s), noTimeZoneData) == "14:32");
}

TEST_CASE("A notification's time is read in the given zone")
{
	// Central European Summer Time is two hours ahead of UTC on 6 October.
	CHECK(Graphite::FormatClockTime(UtcOn6October(12h), std::chrono::locate_zone("Europe/Warsaw")) == "14:00");
}
