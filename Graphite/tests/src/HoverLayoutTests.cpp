#include <doctest/doctest.h>

#include "Editor/HoverLayout.h"

#include <string>
#include <vector>

using namespace Graphite;

using Lines = std::vector<std::string>;

TEST_CASE("A preview loses the indentation its lines share")
{
	CHECK(DedentPreviewLines({"        virtual void OnUpdate(Timestep ts) { (void)ts; }"}, 4) ==
		  Lines{"virtual void OnUpdate(Timestep ts) { (void)ts; }"});

	CHECK(DedentPreviewLines({"    void Run()", "    {", "        Step();", "    }"}, 4) ==
		  Lines{"void Run()", "{", "    Step();", "}"});
}

TEST_CASE("A preview starting at the left edge keeps its indentation")
{
	CHECK(DedentPreviewLines({"int Long(int x)", "{", "    x += 1;", "}"}, 4) ==
		  Lines{"int Long(int x)", "{", "    x += 1;", "}"});
}

TEST_CASE("Tabs are expanded before the shared indentation is measured")
{
	// One tab and four spaces are the same indentation at a tab width of four.
	CHECK(DedentPreviewLines({"\tvoid Run()", "    {", "\t\tStep();", "\t}"}, 4) ==
		  Lines{"void Run()", "{", "    Step();", "}"});

	// A tab after text still lands on its stop, measured from the line's own start.
	CHECK(DedentPreviewLines({"\tint a;\t// x"}, 4) == Lines{"int a;  // x"});
}

TEST_CASE("Blank lines neither count toward the shared indentation nor keep their own")
{
	CHECK(DedentPreviewLines({"    int a;", "", "  ", "    int b;"}, 4) ==
		  Lines{"int a;", "", "", "int b;"});
	CHECK(DedentPreviewLines({"  int a;", "        ", "  int b;"}, 4) ==
		  Lines{"int a;", "", "int b;"});
}

TEST_CASE("A preview of blank lines alone is left as it is, and an empty one stays empty")
{
	CHECK(DedentPreviewLines({"", "  "}, 4) == Lines{"", "  "});
	CHECK(DedentPreviewLines({}, 4).empty());
}

namespace {
	PreviewMetrics Metrics()
	{
		PreviewMetrics metrics;
		metrics.CellWidth = 6.0f;
		metrics.LineHeight = 13.0f;
		metrics.ScrollbarSize = 14.0f;
		metrics.MaxWidth = 300.0f;
		metrics.MaxRows = 20;
		return metrics;
	}
}

TEST_CASE("A short preview is exactly as wide as its widest line and as tall as its rows")
{
	const PreviewBox box = PreviewBoxSize(23, 5, Metrics());

	CHECK(box.Width == doctest::Approx(23.0f * 6.0f));
	CHECK(box.Height == doctest::Approx(5.0f * 13.0f));
}

TEST_CASE("A tall preview keeps a cell clear between its widest line and the scrollbar")
{
	const PreviewBox box = PreviewBoxSize(23, 31, Metrics());

	CHECK(box.Width == doctest::Approx(23.0f * 6.0f + 6.0f + 14.0f));
	CHECK(box.Height == doctest::Approx(20.0f * 13.0f));
}

TEST_CASE("A preview wider than the cap is cut to it, with room for the horizontal scrollbar")
{
	const PreviewBox box = PreviewBoxSize(80, 5, Metrics());

	CHECK(box.Width == doctest::Approx(300.0f));
	CHECK(box.Height == doctest::Approx(5.0f * 13.0f + 14.0f));
}

TEST_CASE("A fractional cell width rounds the preview up, never down past its last glyph")
{
	PreviewMetrics metrics = Metrics();
	metrics.CellWidth = 5.87f;

	const PreviewBox box = PreviewBoxSize(23, 5, metrics);

	CHECK(box.Width >= 23.0f * 5.87f);
	CHECK(box.Width < 23.0f * 5.87f + 1.0f);
}

namespace {
	// An editor surface from (100, 50) to (900, 650), and a token at row 300.
	constexpr ImVec2 kBoundsMin(100.0f, 50.0f);
	constexpr ImVec2 kBoundsMax(900.0f, 650.0f);
	constexpr ImVec2 kAnchorMin(400.0f, 300.0f);
	constexpr ImVec2 kAnchorMax(440.0f, 313.0f);
}

TEST_CASE("A tooltip that fits below the token goes below it")
{
	const TooltipPlacement placed =
		PlaceTooltip(kAnchorMin, kAnchorMax, ImVec2(200.0f, 300.0f), kBoundsMin, kBoundsMax);

	CHECK(placed.Position.x == doctest::Approx(400.0f));
	CHECK(placed.Position.y == doctest::Approx(313.0f));
	CHECK(placed.Pivot.y == doctest::Approx(0.0f));
	CHECK(placed.MaxHeight == doctest::Approx(650.0f - 313.0f));
}

TEST_CASE("A tooltip too tall for the room below the token goes above it")
{
	// 337 below, 250 above: the tooltip of 240 fits only above.
	const ImVec2 anchorMin(400.0f, 300.0f);
	const ImVec2 anchorMax(440.0f, 313.0f);
	const TooltipPlacement placed = PlaceTooltip(anchorMin, anchorMax, ImVec2(200.0f, 240.0f),
												 ImVec2(100.0f, 50.0f), ImVec2(900.0f, 500.0f));

	CHECK(placed.Position.y == doctest::Approx(300.0f));
	CHECK(placed.Pivot.y == doctest::Approx(1.0f));
	CHECK(placed.MaxHeight == doctest::Approx(250.0f));
}

TEST_CASE("The editor's own bottom edge, not the screen's, decides the flip")
{
	// Room enough below on the screen, but the editor ends 60 below the token: the panel under
	// it is not the tooltip's to cover.
	const TooltipPlacement placed = PlaceTooltip(kAnchorMin, kAnchorMax, ImVec2(200.0f, 120.0f),
												 kBoundsMin, ImVec2(900.0f, 373.0f));

	CHECK(placed.Pivot.y == doctest::Approx(1.0f));
	CHECK(placed.Position.y == doctest::Approx(300.0f));
}

TEST_CASE("A tooltip that fits neither way takes the roomier side and is held to it")
{
	const TooltipPlacement above = PlaceTooltip(kAnchorMin, kAnchorMax, ImVec2(200.0f, 500.0f),
												kBoundsMin, ImVec2(900.0f, 400.0f));
	CHECK(above.Pivot.y == doctest::Approx(1.0f));
	CHECK(above.MaxHeight == doctest::Approx(250.0f));

	const TooltipPlacement below = PlaceTooltip(kAnchorMin, kAnchorMax, ImVec2(200.0f, 500.0f),
												ImVec2(100.0f, 200.0f), kBoundsMax);
	CHECK(below.Pivot.y == doctest::Approx(0.0f));
	CHECK(below.MaxHeight == doctest::Approx(650.0f - 313.0f));
}

TEST_CASE("A tooltip is kept inside the editor's right edge, and never past its left")
{
	const TooltipPlacement right = PlaceTooltip(ImVec2(850.0f, 300.0f), ImVec2(870.0f, 313.0f),
												ImVec2(200.0f, 100.0f), kBoundsMin, kBoundsMax);
	CHECK(right.Position.x == doctest::Approx(700.0f));

	const TooltipPlacement wide = PlaceTooltip(kAnchorMin, kAnchorMax, ImVec2(1000.0f, 100.0f),
											   kBoundsMin, kBoundsMax);
	CHECK(wide.Position.x == doctest::Approx(100.0f));
}
