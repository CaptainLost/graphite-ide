#include <doctest/doctest.h>

#include "UI/SplitResults.h"

using Graphite::DefaultResultsHeight;

TEST_CASE("The results get 65 percent of what is left after the divider")
{
	CHECK(DefaultResultsHeight(300.0f, 20.0f) == doctest::Approx(182.0f));
	CHECK(DefaultResultsHeight(120.0f, 0.0f) == doctest::Approx(78.0f));
}

TEST_CASE("A panel shorter than its divider gives the results nothing rather than a negative height")
{
	CHECK(DefaultResultsHeight(10.0f, 20.0f) == 0.0f);
}
