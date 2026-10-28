#include <doctest/doctest.h>

#include "UI/WheelNotches.h"

TEST_CASE("Fractions of a wheel notch add up to whole notches in either direction")
{
	float pending = 0.0f;

	CHECK(Graphite::TakeWholeNotches(pending, 0.25f) == 0);
	CHECK(Graphite::TakeWholeNotches(pending, 0.5f) == 0);
	CHECK(Graphite::TakeWholeNotches(pending, 0.5f) == 1);
	CHECK(pending == doctest::Approx(0.25f));

	CHECK(Graphite::TakeWholeNotches(pending, -0.5f) == 0);
	CHECK(Graphite::TakeWholeNotches(pending, -0.75f) == -1);
	CHECK(pending == doctest::Approx(0.0f));
}

TEST_CASE("A turn of several notches is taken whole, keeping its fraction")
{
	float pending = 0.0f;

	CHECK(Graphite::TakeWholeNotches(pending, -2.5f) == -2);
	CHECK(pending == doctest::Approx(-0.5f));
}
