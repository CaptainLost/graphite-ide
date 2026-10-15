#include <doctest/doctest.h>

#include "GitFixtures.h"
#include "Git/Branches/BranchAge.h"

namespace {

	constexpr long long kNowSeconds = 1700000000;
	constexpr long long kMinute = 60;
	constexpr long long kHour = 60 * kMinute;
	constexpr long long kDay = 24 * kHour;

	std::string AgeOf(long long secondsAgo)
	{
		return Graphite::FormatBranchAge(SystemSecondsAt(kNowSeconds - secondsAgo), SystemSecondsAt(kNowSeconds));
	}
}

TEST_CASE("A tip under a minute old or in the future is now")
{
	CHECK(AgeOf(59) == "now");
	CHECK(AgeOf(-kHour) == "now");
}

TEST_CASE("Minutes and hours and days are counted down to whole units")
{
	CHECK(AgeOf(kMinute) == "1m");
	CHECK(AgeOf(59 * kMinute) == "59m");
	CHECK(AgeOf(kHour) == "1h");
	CHECK(AgeOf(23 * kHour) == "23h");
	CHECK(AgeOf(kDay) == "1d");
	CHECK(AgeOf(13 * kDay) == "13d");
}

TEST_CASE("Weeks run from two to eight and months to twelve")
{
	CHECK(AgeOf(14 * kDay) == "2w");
	CHECK(AgeOf(62 * kDay) == "8w");
	CHECK(AgeOf(63 * kDay) == "2mo");
	CHECK(AgeOf(364 * kDay) == "12mo");
}

TEST_CASE("A year or more is counted in years")
{
	CHECK(AgeOf(365 * kDay) == "1y");
	CHECK(AgeOf(800 * kDay) == "2y");
}