#include <doctest/doctest.h>

#include "Editor/Hover/HoverLayout.h"

#include <ostream>
#include <string>
#include <string_view>
#include <vector>

using namespace Graphite;

using Lines = std::vector<std::string>;

TEST_CASE("A preview loses the indentation its lines share")
{
	CHECK(DedentPreview({"        virtual void OnUpdate(Timestep ts) { (void)ts; }"}, 4).Lines ==
		  Lines{"virtual void OnUpdate(Timestep ts) { (void)ts; }"});

	CHECK(DedentPreview({"    void Run()", "    {", "        Step();", "    }"}, 4).Lines ==
		  Lines{"void Run()", "{", "    Step();", "}"});
}

TEST_CASE("A preview starting at the left edge keeps its indentation")
{
	CHECK(DedentPreview({"int Long(int x)", "{", "    x += 1;", "}"}, 4).Lines ==
		  Lines{"int Long(int x)", "{", "    x += 1;", "}"});
}

TEST_CASE("Tabs are expanded before the shared indentation is measured")
{
	// One tab and four spaces are the same indentation at a tab width of four.
	CHECK(DedentPreview({"\tvoid Run()", "    {", "\t\tStep();", "\t}"}, 4).Lines ==
		  Lines{"void Run()", "{", "    Step();", "}"});

	// A tab after text still lands on its stop, measured from the line's own start.
	CHECK(DedentPreview({"\tint a;\t// x"}, 4).Lines == Lines{"int a;  // x"});
}

TEST_CASE("Blank lines neither count toward the shared indentation nor keep their own")
{
	CHECK(DedentPreview({"    int a;", "", "  ", "    int b;"}, 4).Lines ==
		  Lines{"int a;", "", "", "int b;"});
	CHECK(DedentPreview({"  int a;", "        ", "  int b;"}, 4).Lines ==
		  Lines{"int a;", "", "int b;"});
}

TEST_CASE("A preview of blank lines alone is left as it is, and an empty one stays empty")
{
	CHECK(DedentPreview({"", "  "}, 4).Lines == Lines{"", "  "});
	CHECK(DedentPreview({}, 4).Lines.empty());
}

TEST_CASE("A preview says how many columns it took off, and none when it took nothing")
{
	CHECK(DedentPreview({"\t\tint a;", "", "\t\t\tint b;"}, 4).Removed == 8);
	CHECK(DedentPreview({"int a;", "    int b;"}, 4).Removed == 0);
	CHECK(DedentPreview({"", "  "}, 4).Removed == 0);
}

TEST_CASE("A server's spans move left with the indentation the preview took off")
{
	const std::vector<SemanticSpan> spans = {{8, 14, TokenKind::SemanticType},
											 {20, 23, TokenKind::SemanticFunction}};

	const std::vector<SemanticSpan> moved = DedentSpans(spans, 8);

	REQUIRE(moved.size() == 2);
	CHECK(moved[0].From == 0);
	CHECK(moved[0].To == 6);
	CHECK(moved[0].Kind == TokenKind::SemanticType);
	CHECK(moved[1].From == 12);
	CHECK(moved[1].To == 15);
}

TEST_CASE("A span inside the indentation the preview took off is dropped")
{
	// Only a span on a line that is not the preview's own could start there; nothing is left
	// for it to colour.
	const std::vector<SemanticSpan> moved = DedentSpans({{2, 6, TokenKind::SemanticType}}, 8);

	CHECK(moved.empty());
}

TEST_CASE("A preview is as wide as its widest line")
{
	CHECK(PreviewWidth(23, 6.0f, false) == doctest::Approx(23.0f * 6.0f));
}

TEST_CASE("A preview beside a scrollbar keeps a cell clear between its widest line and it")
{
	CHECK(PreviewWidth(23, 6.0f, true) == doctest::Approx(24.0f * 6.0f));
}

TEST_CASE("A fractional cell width rounds the preview up, never down past its last glyph")
{
	const float width = PreviewWidth(23, 5.87f, false);

	CHECK(width >= 23.0f * 5.87f);
	CHECK(width < 23.0f * 5.87f + 1.0f);
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

TEST_CASE("A declaration's code splits into its lines")
{
	CHECK(Graphite::CodeLinesOf("int Sum()") == std::vector<std::string_view>{"int Sum()"});
	CHECK(Graphite::CodeLinesOf("void Draw(\r\n    int width)") == std::vector<std::string_view>{"void Draw(", "    int width)"});
}

TEST_CASE("Only a lexer-coloured language has its declaration's identifiers named by the tooltip")
{
	CHECK(Graphite::NeedsDeclarationSpans(Graphite::LanguageId::Cpp));
	CHECK_FALSE(Graphite::NeedsDeclarationSpans(Graphite::LanguageId::CSharp));
	CHECK_FALSE(Graphite::NeedsDeclarationSpans(Graphite::LanguageId::PlainText));
}
