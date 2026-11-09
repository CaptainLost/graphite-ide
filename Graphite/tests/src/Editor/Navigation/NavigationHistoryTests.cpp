#include <doctest/doctest.h>

#include "Editor/Navigation/NavigationHistory.h"

using Graphite::NavigationHistory;
using Graphite::NavigationPoint;

namespace {
	NavigationPoint At(const char* path, std::size_t line)
	{
		return NavigationPoint{.Path = path, .Line = line};
	}
}

TEST_CASE("Back returns to where a jump left from, and Forward goes to where Back left")
{
	NavigationHistory history;
	history.NoteDeparture(At("main.cpp", 10));

	const std::optional<NavigationPoint> back = history.Back(At("util.cpp", 4));
	REQUIRE(back.has_value());
	CHECK(*back == At("main.cpp", 10));
	CHECK_FALSE(history.CanGoBack());

	const std::optional<NavigationPoint> forward = history.Forward(At("main.cpp", 10));
	REQUIRE(forward.has_value());
	CHECK(*forward == At("util.cpp", 4));
	CHECK(history.CanGoBack());
}

TEST_CASE("A new jump forgets what Forward could reach and a repeated departure is noted once")
{
	NavigationHistory history;
	history.NoteDeparture(At("a.cpp", 1));
	history.NoteDeparture(At("b.cpp", 2));
	REQUIRE(history.Back(At("c.cpp", 3)).has_value());
	REQUIRE(history.CanGoForward());

	history.NoteDeparture(At("b.cpp", 2));
	CHECK_FALSE(history.CanGoForward());

	history.NoteDeparture(At("b.cpp", 2));
	REQUIRE(history.Back(At("d.cpp", 4)) == At("b.cpp", 2));
	CHECK(history.Back(At("b.cpp", 2)) == At("a.cpp", 1));
	CHECK_FALSE(history.Back(At("a.cpp", 1)).has_value());
}

TEST_CASE("The history keeps only the latest departures")
{
	NavigationHistory history;

	for (std::size_t line = 0; line < NavigationHistory::kLongestHistory + 5; ++line)
	{
		history.NoteDeparture(At("a.cpp", line));
	}

	std::size_t steps = 0;
	NavigationPoint current = At("z.cpp", 0);

	while (const std::optional<NavigationPoint> back = history.Back(current))
	{
		current = *back;
		++steps;
	}

	CHECK(steps == NavigationHistory::kLongestHistory);
	CHECK(current == At("a.cpp", 5));
}
