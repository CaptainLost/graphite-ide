#include <doctest/doctest.h>

#include "Git/DiffPanes.h"

using Graphite::LayoutSideBySide;
using Graphite::SideBySideLayout;

namespace {

	// How far the region's scrollbar travels, the way ImGui works it out: it measures content
	// from the padded corner but compares against the unpadded width, so it adds a padding back
	// on each side. Zero means no scrollbar appears at all. Asserting on this rather than on
	// ContentWidth directly is what makes these tests about what the reader sees.
	float ScrollRange(const SideBySideLayout& layout, float availableWidth, float padding)
	{
		return layout.ContentWidth + padding * 2.0f - availableWidth;
	}
}

TEST_CASE("Each half of a side-by-side diff takes half the visible width")
{
	const SideBySideLayout layout = LayoutSideBySide(800.0f, 100.0f, 8.0f);

	CHECK(layout.PaneWidth == doctest::Approx(400.0f));
	CHECK(layout.RightPaneX == doctest::Approx(400.0f));
}

TEST_CASE("Lines that fit leave nothing to scroll, so no horizontal scrollbar appears")
{
	const SideBySideLayout layout = LayoutSideBySide(800.0f, 100.0f, 8.0f);

	CHECK(ScrollRange(layout, 800.0f, 8.0f) == doctest::Approx(0.0f));
}

TEST_CASE("A line reaching exactly the inner edge of its pane still leaves nothing to scroll")
{
	// Its text starts a padding in, so the line that just fits is a padding narrower than the
	// pane — the case that decides whether a scrollbar appears one pixel too early.
	const SideBySideLayout layout = LayoutSideBySide(800.0f, 392.0f, 8.0f);

	CHECK(ScrollRange(layout, 800.0f, 8.0f) == doctest::Approx(0.0f));
}

TEST_CASE("A line one pixel wider than its pane becomes scrollable by exactly that pixel")
{
	const SideBySideLayout layout = LayoutSideBySide(800.0f, 393.0f, 8.0f);

	CHECK(ScrollRange(layout, 800.0f, 8.0f) == doctest::Approx(1.0f));
}

TEST_CASE("Scrolling stops where the widest line ends rather than running into blank space")
{
	const SideBySideLayout layout = LayoutSideBySide(800.0f, 1000.0f, 8.0f);

	// Scrolled all the way, the line's last pixel sits at the inner edge of its own pane.
	CHECK(ScrollRange(layout, 800.0f, 8.0f) == doctest::Approx(1000.0f + 8.0f - 400.0f));
}

TEST_CASE("Without padding the widest line is measured against the pane alone")
{
	const SideBySideLayout layout = LayoutSideBySide(800.0f, 1000.0f, 0.0f);

	CHECK(ScrollRange(layout, 800.0f, 0.0f) == doctest::Approx(600.0f));
	CHECK(LayoutSideBySide(800.0f, 400.0f, 0.0f).ContentWidth == doctest::Approx(800.0f));
}

TEST_CASE("A region ImGui reports as negative collapses to zero rather than inverting the panes")
{
	const SideBySideLayout layout = LayoutSideBySide(-40.0f, 0.0f, 8.0f);

	CHECK(layout.PaneWidth == doctest::Approx(0.0f));
	CHECK(layout.RightPaneX == doctest::Approx(0.0f));
}

TEST_CASE("A collapsed region still lets the reader scroll to a line drawn in it")
{
	// The panes have no width, but the line does — the reader can still reach it rather than
	// being left with content narrower than the text inside it.
	const SideBySideLayout layout = LayoutSideBySide(0.0f, 120.0f, 8.0f);

	CHECK(ScrollRange(layout, 0.0f, 8.0f) == doctest::Approx(128.0f));
}

TEST_CASE("A padding wider than the region does not fold the content back on itself")
{
	const SideBySideLayout layout = LayoutSideBySide(20.0f, 0.0f, 40.0f);

	// Nothing to reveal, and the scrollbar must not run backwards to say so.
	CHECK(ScrollRange(layout, 20.0f, 40.0f) >= 0.0f);
}
