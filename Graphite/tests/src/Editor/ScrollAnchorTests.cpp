#include <doctest/doctest.h>

#include "Editor/ScrollAnchor.h"

using namespace Graphite;

namespace {

	constexpr float kLineHeight = 16.0f;

	ScrollAnchor AtRow(std::size_t screenRow, float scrollY)
	{
		ScrollAnchor anchor;
		anchor.Line = 20;
		anchor.ScreenRow = screenRow;
		anchor.ScrollY = scrollY;

		return anchor;
	}
}

TEST_CASE("A row that has not moved asks for no correction")
{
	CHECK_FALSE(ScrollKeepingAnchor(AtRow(20, 320.0f), 20, kLineHeight).has_value());
}

TEST_CASE("Rows inserted above the viewport push the scroll down by exactly their height")
{
	const std::optional<float> scroll = ScrollKeepingAnchor(AtRow(20, 320.0f), 59, kLineHeight);

	REQUIRE(scroll.has_value());
	CHECK(*scroll == doctest::Approx(320.0f + 39.0f * kLineHeight));
}

TEST_CASE("Rows removed above the viewport pull the scroll back up")
{
	const std::optional<float> scroll = ScrollKeepingAnchor(AtRow(20, 320.0f), 12, kLineHeight);

	REQUIRE(scroll.has_value());
	CHECK(*scroll == doctest::Approx(320.0f - 8.0f * kLineHeight));
}

TEST_CASE("The scroll never goes above the top of the document")
{
	const std::optional<float> scroll = ScrollKeepingAnchor(AtRow(20, 32.0f), 0, kLineHeight);

	REQUIRE(scroll.has_value());
	CHECK(*scroll == doctest::Approx(0.0f));
}

TEST_CASE("The offset inside the row survives the correction")
{
	const std::optional<float> scroll = ScrollKeepingAnchor(AtRow(20, 327.0f), 21, kLineHeight);

	REQUIRE(scroll.has_value());
	CHECK(*scroll == doctest::Approx(327.0f + kLineHeight));
}

TEST_CASE("A line height of zero corrects nothing")
{
	CHECK_FALSE(ScrollKeepingAnchor(AtRow(20, 320.0f), 59, 0.0f).has_value());
}

TEST_CASE("An unchanged line hands back exactly the row the anchor was taken on")
{
	ScrollAnchor anchor;
	anchor.Line = 20;
	anchor.SubRow = 2;

	CHECK(AnchoredRow(anchor, 100, 5) == 102);
}

TEST_CASE("A line that rewrapped shorter lands on its last row rather than past it")
{
	ScrollAnchor anchor;
	anchor.Line = 20;
	anchor.SubRow = 4;

	CHECK(AnchoredRow(anchor, 100, 2) == 101);
}

TEST_CASE("A line with no rows resolves to where the line begins")
{
	ScrollAnchor anchor;
	anchor.Line = 20;
	anchor.SubRow = 3;

	CHECK(AnchoredRow(anchor, 100, 0) == 100);
}

TEST_CASE("An anchor at a line scrolls that line to the top of the first frame that draws it")
{
	const ScrollAnchor anchor = AnchorAtLine(40);

	CHECK(ScrollKeepingAnchor(anchor, 40, kLineHeight) == 40 * kLineHeight);
}

TEST_CASE("An anchor at line zero asks for no scroll")
{
	CHECK_FALSE(ScrollKeepingAnchor(AnchorAtLine(0), 0, kLineHeight).has_value());
}

TEST_CASE("A pending top line settles once a frame shows it")
{
	PendingTopLine pending{.Line = 40};

	ScrollAnchor drawn;
	drawn.Taken = true;
	drawn.Line = 0;
	CHECK_FALSE(pending.Settle(drawn));

	drawn.Line = 40;
	CHECK(pending.Settle(drawn));
}

TEST_CASE("A pending top line the view never reaches gives up after its frames")
{
	PendingTopLine pending{.Line = 40};

	ScrollAnchor drawn;
	drawn.Taken = true;
	drawn.Line = 12;

	for (int frame = 1; frame < PendingTopLine::kFrames; ++frame)
	{
		CHECK_FALSE(pending.Settle(drawn));
	}

	CHECK(pending.Settle(drawn));
}

TEST_CASE("A pending top line is still wanted while nothing has moved the caret or the view")
{
	const PendingTopLine pending{.Line = 40, .CaretOffset = 812};

	const bool pendingScroll = false;
	const bool userScrolled = false;

	CHECK(pending.StillWanted(812, pendingScroll, userScrolled));
}

TEST_CASE("A pending top line is dropped once the caret leaves where the restore put it")
{
	const PendingTopLine pending{.Line = 40, .CaretOffset = 812};

	const bool pendingScroll = false;
	const bool userScrolled = false;

	CHECK_FALSE(pending.StillWanted(4096, pendingScroll, userScrolled));
}

TEST_CASE("A pending top line is dropped when somebody asked for the caret to be scrolled to")
{
	const PendingTopLine pending{.Line = 40, .CaretOffset = 812};

	const bool pendingScroll = true;
	const bool userScrolled = false;

	CHECK_FALSE(pending.StillWanted(812, pendingScroll, userScrolled));
}

TEST_CASE("A pending top line is dropped when the reader scrolls the view themselves")
{
	const PendingTopLine pending{.Line = 40, .CaretOffset = 812};

	const bool pendingScroll = false;
	const bool userScrolled = true;

	CHECK_FALSE(pending.StillWanted(812, pendingScroll, userScrolled));
}
