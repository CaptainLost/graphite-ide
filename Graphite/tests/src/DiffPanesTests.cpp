#include <doctest/doctest.h>

#include "Git/DiffPanes.h"

using Graphite::SideBySideContentWidth;
using Graphite::SideBySidePanes;
using Graphite::SplitIntoPanes;

namespace {

	// How far the region's scrollbar travels, the way ImGui works it out: it measures content
	// from the padded corner but compares against the inner width, so it adds an inset back on
	// each side. Zero means no scrollbar appears at all. Asserting on this rather than on the
	// content width directly is what makes these tests about what the reader sees.
	//
	// `innerWidth` is the width ImGui compares against, which is why these cases pass it to
	// SideBySideContentWidth as well: the panes are measured against the visible width, which
	// is narrower by the border, and mixing the two up is how the last pixel of the widest
	// line becomes unreachable.
	float ScrollRange(float contentWidth, float innerWidth, float padding)
	{
		return contentWidth + padding * 2.0f - innerWidth;
	}
}

TEST_CASE("Each half of a side-by-side diff takes half of what the reader can see")
{
	const SideBySidePanes panes = SplitIntoPanes(800.0f);

	CHECK(panes.PaneWidth == doctest::Approx(400.0f));
	CHECK(panes.RightPaneX == doctest::Approx(400.0f));
}

TEST_CASE("A region ImGui reports as negative collapses to zero rather than inverting the panes")
{
	const SideBySidePanes panes = SplitIntoPanes(-40.0f);

	CHECK(panes.PaneWidth == doctest::Approx(0.0f));
	CHECK(panes.RightPaneX == doctest::Approx(0.0f));
}

TEST_CASE("Lines that fit leave nothing to scroll, so no horizontal scrollbar appears")
{
	const float content = SideBySideContentWidth(800.0f, 100.0f, 8.0f);

	CHECK(ScrollRange(content, 800.0f, 8.0f) == doctest::Approx(0.0f));
}

TEST_CASE("A line reaching exactly the inner edge of its half still leaves nothing to scroll")
{
	// Its text starts an inset in, so the line that just fits is an inset narrower than the
	// half — the case that decides whether a scrollbar appears one pixel too early.
	const float content = SideBySideContentWidth(800.0f, 392.0f, 8.0f);

	CHECK(ScrollRange(content, 800.0f, 8.0f) == doctest::Approx(0.0f));
}

TEST_CASE("A line one pixel wider than its half becomes scrollable by exactly that pixel")
{
	const float content = SideBySideContentWidth(800.0f, 393.0f, 8.0f);

	CHECK(ScrollRange(content, 800.0f, 8.0f) == doctest::Approx(1.0f));
}

TEST_CASE("Scrolling stops where the widest line ends rather than running into blank space")
{
	const float content = SideBySideContentWidth(800.0f, 1000.0f, 8.0f);

	CHECK(ScrollRange(content, 800.0f, 8.0f) == doctest::Approx(1000.0f + 8.0f - 400.0f));
}

TEST_CASE("Without an inset the widest line is measured against the half alone")
{
	CHECK(ScrollRange(SideBySideContentWidth(800.0f, 1000.0f, 0.0f), 800.0f, 0.0f) ==
		  doctest::Approx(600.0f));
	CHECK(ScrollRange(SideBySideContentWidth(800.0f, 400.0f, 0.0f), 800.0f, 0.0f) ==
		  doctest::Approx(0.0f));
}

TEST_CASE("A collapsed region still lets the reader scroll to a line drawn in it")
{
	// The halves have no width, but the line does — the reader can still reach it rather than
	// being left with content narrower than the text inside it.
	const float content = SideBySideContentWidth(0.0f, 120.0f, 8.0f);

	CHECK(ScrollRange(content, 0.0f, 8.0f) == doctest::Approx(128.0f));
}

TEST_CASE("An inset wider than the region does not fold the content back on itself")
{
	const float content = SideBySideContentWidth(20.0f, 0.0f, 40.0f);

	// Nothing to reveal, and the scrollbar must not run backwards to say so.
	CHECK(content >= 0.0f);
	CHECK(ScrollRange(content, 20.0f, 40.0f) >= 0.0f);
}
