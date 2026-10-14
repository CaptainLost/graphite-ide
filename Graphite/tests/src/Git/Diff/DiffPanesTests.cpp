#include <doctest/doctest.h>

#include "Git/Diff/DiffPanes.h"

using Graphite::SideBySideContentWidth;
using Graphite::SideBySidePanes;
using Graphite::SplitIntoPanes;

namespace {

	float ScrollbarTravelAsImGuiComputesIt(float contentWidth, float innerWidth, float padding)
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
	const float contentWidth = SideBySideContentWidth(800.0f, 100.0f, 8.0f);

	CHECK(ScrollbarTravelAsImGuiComputesIt(contentWidth, 800.0f, 8.0f) == doctest::Approx(0.0f));
}

TEST_CASE("A line reaching exactly the inner edge of its half still leaves nothing to scroll")
{
	const float contentWidth = SideBySideContentWidth(800.0f, 392.0f, 8.0f);

	CHECK(ScrollbarTravelAsImGuiComputesIt(contentWidth, 800.0f, 8.0f) == doctest::Approx(0.0f));
}

TEST_CASE("A line one pixel wider than its half becomes scrollable by exactly that pixel")
{
	const float contentWidth = SideBySideContentWidth(800.0f, 393.0f, 8.0f);

	CHECK(ScrollbarTravelAsImGuiComputesIt(contentWidth, 800.0f, 8.0f) == doctest::Approx(1.0f));
}

TEST_CASE("Scrolling stops where the widest line ends rather than running into blank space")
{
	const float contentWidth = SideBySideContentWidth(800.0f, 1000.0f, 8.0f);

	CHECK(ScrollbarTravelAsImGuiComputesIt(contentWidth, 800.0f, 8.0f) == doctest::Approx(1000.0f + 8.0f - 400.0f));
}

TEST_CASE("Without an inset the widest line is measured against the half alone")
{
	CHECK(ScrollbarTravelAsImGuiComputesIt(SideBySideContentWidth(800.0f, 1000.0f, 0.0f), 800.0f, 0.0f) ==
		  doctest::Approx(600.0f));
	CHECK(ScrollbarTravelAsImGuiComputesIt(SideBySideContentWidth(800.0f, 400.0f, 0.0f), 800.0f, 0.0f) ==
		  doctest::Approx(0.0f));
}

TEST_CASE("A collapsed region still lets the reader scroll to a line drawn in it")
{
	const float contentWidth = SideBySideContentWidth(0.0f, 120.0f, 8.0f);

	CHECK(ScrollbarTravelAsImGuiComputesIt(contentWidth, 0.0f, 8.0f) == doctest::Approx(128.0f));
}

TEST_CASE("An inset wider than the region does not fold the content back on itself")
{
	const float contentWidth = SideBySideContentWidth(20.0f, 0.0f, 40.0f);

	CHECK(contentWidth >= 0.0f);
	CHECK(ScrollbarTravelAsImGuiComputesIt(contentWidth, 20.0f, 40.0f) >= 0.0f);
}
