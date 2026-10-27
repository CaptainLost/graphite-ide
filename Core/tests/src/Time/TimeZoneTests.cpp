#include <doctest/doctest.h>

#include "Core/Time/TimeZone.h"

#include <chrono>
#include <ostream>

namespace {
	using namespace std::chrono_literals;

	std::chrono::sys_seconds NoonUtcOn(std::chrono::year_month_day day)
	{
		return std::chrono::sys_days{day} + 12h;
	}
}

TEST_CASE("Without a zone the offset is zero, so times read as UTC")
{
	const std::chrono::time_zone* const noTimeZoneData = nullptr;

	CHECK(Core::OffsetAt(noTimeZoneData, NoonUtcOn(std::chrono::year{2026} / 10 / 6)) == 0min);
}

TEST_CASE("The offset is the zone's at that moment, summer time included")
{
	const std::chrono::time_zone* const warsaw = std::chrono::locate_zone("Europe/Warsaw");

	CHECK(Core::OffsetAt(warsaw, NoonUtcOn(std::chrono::year{2026} / 1 / 15)) == 60min);
	CHECK(Core::OffsetAt(warsaw, NoonUtcOn(std::chrono::year{2026} / 7 / 15)) == 120min);
}

TEST_CASE("The offset stays the zone's at each moment when asked in turn across a transition")
{
	const std::chrono::time_zone* const warsaw = std::chrono::locate_zone("Europe/Warsaw");
	const std::chrono::time_zone* const utc = std::chrono::locate_zone("UTC");

	// Summer time began in Warsaw at 01:00 UTC on 29 March 2026.
	const std::chrono::sys_seconds summerTimeBegins = std::chrono::sys_days{std::chrono::year{2026} / 3 / 29} + 1h;

	for (int round = 0; round < 2; ++round)
	{
		CHECK(Core::OffsetAt(warsaw, NoonUtcOn(std::chrono::year{2026} / 7 / 15)) == 120min);
		CHECK(Core::OffsetAt(warsaw, NoonUtcOn(std::chrono::year{2026} / 1 / 15)) == 60min);
		CHECK(Core::OffsetAt(warsaw, summerTimeBegins - 1s) == 60min);
		CHECK(Core::OffsetAt(warsaw, summerTimeBegins) == 120min);
		CHECK(Core::OffsetAt(utc, NoonUtcOn(std::chrono::year{2026} / 7 / 15)) == 0min);
	}
}

TEST_CASE("The local zone is found on a machine with time zone data")
{
	CHECK(Core::LocalTimeZone() != nullptr);
}