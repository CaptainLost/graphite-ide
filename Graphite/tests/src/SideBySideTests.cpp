#include <doctest/doctest.h>

#include "UI/SideBySide.h"

#include <initializer_list>
#include <limits>

using Graphite::SideBySideWidths;
using Graphite::SplitSideBySide;

TEST_CASE("A wide room gives the left pane the width it was dragged to")
{
	const SideBySideWidths widths = SplitSideBySide(1000.0f, 400.0f, 150.0f);

	CHECK(widths.Left == 400.0f);
	CHECK(widths.Right == 600.0f);
}

TEST_CASE("A wide room keeps each side its minimum however far the handle is dragged")
{
	const SideBySideWidths towardsLeft = SplitSideBySide(1000.0f, 20.0f, 150.0f);

	CHECK(towardsLeft.Left == 150.0f);
	CHECK(towardsLeft.Right == 850.0f);

	const SideBySideWidths towardsRight = SplitSideBySide(1000.0f, 990.0f, 150.0f);

	CHECK(towardsRight.Left == 850.0f);
	CHECK(towardsRight.Right == 150.0f);
}

TEST_CASE("A room of exactly two minimums gives each side its minimum, wherever the handle was")
{
	for (const float wanted : {0.0f, 150.0f, 300.0f})
	{
		const SideBySideWidths widths = SplitSideBySide(300.0f, wanted, 150.0f);

		CHECK(widths.Left == 150.0f);
		CHECK(widths.Right == 150.0f);
	}
}

TEST_CASE("A room short of two minimums is shared in halves and never overflows")
{
	for (const float wanted : {0.0f, 100.0f, 400.0f})
	{
		const SideBySideWidths widths = SplitSideBySide(200.0f, wanted, 150.0f);

		CHECK(widths.Left == 100.0f);
		CHECK(widths.Right == 100.0f);
		CHECK(widths.Left + widths.Right <= 200.0f);
	}
}

TEST_CASE("A narrow room of an odd width is halved to the fraction")
{
	const SideBySideWidths widths = SplitSideBySide(201.0f, 400.0f, 150.0f);

	CHECK(widths.Left == 100.5f);
	CHECK(widths.Right == 100.5f);
}

TEST_CASE("A drag by a fraction of a pixel is kept rather than rounded away")
{
	const SideBySideWidths widths = SplitSideBySide(1000.0f, 400.25f, 150.0f);

	CHECK(widths.Left == 400.25f);
	CHECK(widths.Right == 599.75f);
}

TEST_CASE("A room with no width to give leaves both panes at zero")
{
	for (const float room : {0.0f, -1.0f, -500.0f, std::numeric_limits<float>::quiet_NaN()})
	{
		const SideBySideWidths widths = SplitSideBySide(room, 400.0f, 150.0f);

		CHECK(widths.Left == 0.0f);
		CHECK(widths.Right == 0.0f);
	}
}

TEST_CASE("A width not picked yet, or not a number, is taken as the left's minimum")
{
	const SideBySideWidths negative = SplitSideBySide(1000.0f, -1.0f, 150.0f);

	CHECK(negative.Left == 150.0f);
	CHECK(negative.Right == 850.0f);

	const SideBySideWidths nan = SplitSideBySide(1000.0f, std::numeric_limits<float>::quiet_NaN(), 150.0f);

	CHECK(nan.Left == 150.0f);
	CHECK(nan.Right == 850.0f);
}

TEST_CASE("A minimum of zero or less lets the handle go to either edge")
{
	const SideBySideWidths widths = SplitSideBySide(500.0f, 900.0f, -10.0f);

	CHECK(widths.Left == 500.0f);
	CHECK(widths.Right == 0.0f);
}
