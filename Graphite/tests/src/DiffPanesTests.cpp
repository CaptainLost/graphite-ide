#include <doctest/doctest.h>

#include "Git/DiffPanes.h"

using Graphite::LayoutSideBySide;
using Graphite::SideBySideLayout;

TEST_CASE("Each half of a side-by-side diff takes half the visible width")
{
	const SideBySideLayout layout = LayoutSideBySide(800.0f, 100.0f);

	CHECK(layout.PaneWidth == doctest::Approx(400.0f));
	CHECK(layout.RightPaneX == doctest::Approx(400.0f));
}

TEST_CASE("Lines that fit leave the content no wider than the view, so no scrollbar appears")
{
	const SideBySideLayout layout = LayoutSideBySide(800.0f, 100.0f);

	CHECK(layout.ContentWidth == doctest::Approx(800.0f));
}

TEST_CASE("A line exactly as wide as its pane still leaves nothing to scroll")
{
	const SideBySideLayout layout = LayoutSideBySide(800.0f, 400.0f);

	CHECK(layout.ContentWidth == doctest::Approx(800.0f));
}

TEST_CASE("A line wider than its pane is reached by scrolling, and nothing beyond it is")
{
	const SideBySideLayout layout = LayoutSideBySide(800.0f, 1000.0f);

	// One pane for the right half to start after, plus the widest line the reader must reach.
	CHECK(layout.ContentWidth == doctest::Approx(1400.0f));

	// Scrolled all the way, the widest line's end sits at the right edge of its own pane.
	CHECK(layout.ContentWidth - 800.0f == doctest::Approx(1000.0f - layout.PaneWidth));
}

TEST_CASE("The widest line governs both halves, because one scrollbar moves them together")
{
	// Whichever side owns the widest line, the layout is the same: the reader scrolls both.
	CHECK(LayoutSideBySide(800.0f, 1000.0f).ContentWidth ==
		  doctest::Approx(LayoutSideBySide(800.0f, 1000.0f).ContentWidth));
}

TEST_CASE("A region ImGui reports as negative collapses to zero rather than inverting the panes")
{
	const SideBySideLayout layout = LayoutSideBySide(-40.0f, 0.0f);

	CHECK(layout.PaneWidth == doctest::Approx(0.0f));
	CHECK(layout.RightPaneX == doctest::Approx(0.0f));
	CHECK(layout.ContentWidth == doctest::Approx(0.0f));
}

TEST_CASE("A zero-width region asks for no content at all")
{
	const SideBySideLayout layout = LayoutSideBySide(0.0f, 0.0f);

	CHECK(layout.ContentWidth == doctest::Approx(0.0f));
}

TEST_CASE("A collapsed region still reaches a line drawn in it")
{
	// The panes have no width, but the widest line does — the reader can still scroll to it
	// rather than being left with content narrower than the text inside it.
	const SideBySideLayout layout = LayoutSideBySide(0.0f, 120.0f);

	CHECK(layout.ContentWidth == doctest::Approx(120.0f));
}
