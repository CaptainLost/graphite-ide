#include <doctest/doctest.h>

#include "Core/Base/Count.h"

#include <cstdint>
#include <limits>

TEST_CASE("A count below a thousand has no separator")
{
	CHECK(Core::FormatCount(0) == "0");
	CHECK(Core::FormatCount(7) == "7");
	CHECK(Core::FormatCount(999) == "999");
}

TEST_CASE("A count from a thousand up is grouped in threes from the right")
{
	CHECK(Core::FormatCount(1000) == "1,000");
	CHECK(Core::FormatCount(1739) == "1,739");
	CHECK(Core::FormatCount(20000) == "20,000");
	CHECK(Core::FormatCount(1200000) == "1,200,000");
}

TEST_CASE("The largest count still groups every digit")
{
	CHECK(Core::FormatCount(std::numeric_limits<std::uint64_t>::max()) == "18,446,744,073,709,551,615");
}
