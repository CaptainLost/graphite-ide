#include <doctest/doctest.h>

#include "Editor/ScrollAnchor.h"

using namespace Graphite;

namespace {

	constexpr float kLineHeight = 16.0f;

	// An anchor taken while the viewport's top edge sat on `screenRow`. Which line it names does
	// not enter the arithmetic — that is the caller's half, and it is what turns a line back into
	// a screen row before calling in here.
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
	// Nothing to write back. The caller leaves the scroll alone rather than setting it to what
	// it already is, which on a frame the user is dragging the scrollbar would fight them.
	CHECK_FALSE(ScrollKeepingAnchor(AtRow(20, 320.0f), 20, kLineHeight).has_value());
}

TEST_CASE("Rows inserted above the viewport push the scroll down by exactly their height")
{
	// What a server's reply does: thirty-nine lens rows land above the viewport, so the row that
	// was at the top now draws thirty-nine rows lower and the pixel scroll must follow it.
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
	// More rows vanished from above than there was scroll to give back — every anchor above the
	// viewport disappearing at once, say. A negative offset is not a position.
	const std::optional<float> scroll = ScrollKeepingAnchor(AtRow(20, 32.0f), 0, kLineHeight);

	REQUIRE(scroll.has_value());
	CHECK(*scroll == doctest::Approx(0.0f));
}

TEST_CASE("The offset inside the row survives the correction")
{
	// The viewport rarely sits on a row boundary. Carrying the remainder is what keeps the
	// correction invisible rather than merely close.
	const std::optional<float> scroll = ScrollKeepingAnchor(AtRow(20, 327.0f), 21, kLineHeight);

	REQUIRE(scroll.has_value());
	CHECK(*scroll == doctest::Approx(327.0f + kLineHeight));
}

TEST_CASE("A line height of zero corrects nothing")
{
	// The first frame of a document, before metrics are known. Without the guard this would
	// return the anchor's own scroll unchanged and write a stale offset back over a live one.
	CHECK_FALSE(ScrollKeepingAnchor(AtRow(20, 320.0f), 59, 0.0f).has_value());
}

TEST_CASE("An unchanged line hands back exactly the row the anchor was taken on")
{
	ScrollAnchor anchor;
	anchor.Line = 20;
	anchor.SubRow = 2;

	// The round trip has to be exact, or the correction fires on frames where nothing moved and
	// the reader is fought while scrolling.
	CHECK(AnchoredRow(anchor, 100, 5) == 102);
}

TEST_CASE("A line that rewrapped shorter lands on its last row rather than past it")
{
	ScrollAnchor anchor;
	anchor.Line = 20;
	anchor.SubRow = 4;

	// A wider window fits the line into two rows where it used to need five. The reader was
	// inside it; the nearest place left is its end.
	CHECK(AnchoredRow(anchor, 100, 2) == 101);
}

TEST_CASE("A line with no rows resolves to where the line begins")
{
	ScrollAnchor anchor;
	anchor.Line = 20;
	anchor.SubRow = 3;

	// Out of range for the map, which answers zero rows. Subtracting one from that would index
	// far past the end of the document.
	CHECK(AnchoredRow(anchor, 100, 0) == 100);
}

TEST_CASE("An anchor at a line scrolls that line to the top of the first frame that draws it")
{
	const ScrollAnchor anchor = AnchorAtLine(40);

	// The line draws at screen row 40 when nothing above it adds rows.
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
