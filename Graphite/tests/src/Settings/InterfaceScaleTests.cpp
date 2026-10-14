#include <doctest/doctest.h>

#include "Settings/InterfaceScale.h"

#include <string>

using Graphite::InterfaceScalePercentOrDefault;
using Graphite::kInterfaceScalePercents;

TEST_CASE("An absent or unreadable interface scale means the system's own size")
{
	CHECK(InterfaceScalePercentOrDefault("") == 100);
	CHECK(InterfaceScalePercentOrDefault("large") == 100);
	CHECK(InterfaceScalePercentOrDefault("0") == 100);
	CHECK(InterfaceScalePercentOrDefault("-125") == 100);
}

TEST_CASE("A stored interface scale that is offered is read back as it was written")
{
	for (const int percent : kInterfaceScalePercents)
	{
		CHECK(InterfaceScalePercentOrDefault(std::to_string(percent)) == percent);
	}
}

TEST_CASE("A stored interface scale that is not offered settles on the nearest that is")
{
	CHECK(InterfaceScalePercentOrDefault("130") == 125);
	CHECK(InterfaceScalePercentOrDefault("140") == 150);
	CHECK(InterfaceScalePercentOrDefault("10") == kInterfaceScalePercents.front());
	CHECK(InterfaceScalePercentOrDefault("900") == kInterfaceScalePercents.back());
}
