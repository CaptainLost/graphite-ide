#include <doctest/doctest.h>

#include "Core/Text/WrapLayout.h"
#include "Core/Text/Columns.h"
#include "Core/Text/Encoding.h"
#include "Core/Text/RowMap.h"
#include "Core/Text/TextBuffer.h"

#include <chrono>
#include <string>
#include <vector>

using namespace Core;

TEST_CASE("A line that fits does not break")
{
	CHECK(WrapColumns("", 10, 4).empty());
	CHECK(WrapColumns("short", 10, 4).empty());
}

TEST_CASE("A line exactly as wide as the row does not break")
{
	// Ten columns in a ten column row: the last character lands on the last cell,
	// and a break after it would make an empty trailing row.
	CHECK(WrapColumns("0123456789", 10, 4).empty());
}

TEST_CASE("A break goes after the last space that fits")
{
	// Columns: "alpha beta gamma", limit 12. The space at column 10 is the last
	// one at or before the limit, so the second row starts at column 11.
	const std::vector<std::size_t> breaks = WrapColumns("alpha beta gamma", 12, 4);

	REQUIRE(breaks.size() == 1);
	CHECK(breaks[0] == 11);
}

TEST_CASE("A word longer than the row breaks hard at the limit")
{
	const std::vector<std::size_t> breaks = WrapColumns("aaaaaaaaaaaaaaa", 5, 4);

	REQUIRE(breaks.size() == 2);
	CHECK(breaks[0] == 5);
	CHECK(breaks[1] == 10);
}

TEST_CASE("Runs of spaces stay on the row they end")
{
	// "aa    bb" with a limit of 6: breaking after the run leaves the next row
	// starting at 'b', never at a space.
	const std::vector<std::size_t> breaks = WrapColumns("aa    bb", 6, 4);

	REQUIRE(breaks.size() == 1);
	CHECK(breaks[0] == 6);
}

TEST_CASE("A tab counts as the columns it advances, not as one character")
{
	// "\tabcdef" with tabWidth 4 occupies 10 columns: 4 for the tab, 6 for the
	// letters. With a limit of 6 the break falls inside the letters, at column 6.
	const std::vector<std::size_t> breaks = WrapColumns("\tabcdef", 6, 4);

	REQUIRE(breaks.size() == 1);
	CHECK(breaks[0] == 6);
}

TEST_CASE("A character outside ASCII is one column whatever its byte length")
{
	// "ąąąą" is four characters and eight bytes. With a limit of 2 it breaks
	// twice, on column boundaries, never mid-character.
	const std::vector<std::size_t> breaks = WrapColumns("\xC4\x85\xC4\x85\xC4\x85\xC4\x85", 2, 4);

	REQUIRE(breaks.size() == 1);
	CHECK(breaks[0] == 2);
}

TEST_CASE("A degenerate row width still terminates")
{
	// A window dragged narrower than one cell is a layout state, not an error.
	CHECK(WrapColumns("abc", 1, 4).size() == 2);
	CHECK(WrapColumns("abc", 0, 4).size() == 2);
}

TEST_CASE("A space run reaching past the limit keeps the whole run on its row")
{
	// The run is invisible, so letting it overflow the row is right; starting the next
	// row with a space is not.
	const std::vector<std::size_t> breaks = WrapColumns("aa      bb", 4, 4);

	REQUIRE(breaks.size() == 1);
	CHECK(breaks[0] == 8);
}

TEST_CASE("A line of nothing but spaces never breaks")
{
	CHECK(WrapColumns("      ", 3, 4).empty());
}

TEST_CASE("A hard break never lands inside a tab")
{
	// Columns: "aaaa" is 0-3, the tab spans 4-7, "bbbb" is 8-11. Breaking at the limit of
	// 6 would split the tab, so the break moves back to where the tab starts.
	const std::vector<std::size_t> breaks = WrapColumns("aaaa\tbbbb", 6, 4);

	REQUIRE(!breaks.empty());
	CHECK(breaks[0] == 4);
}

TEST_CASE("A tab wider than the row still advances")
{
	// The tab starts at the row's own start, so it cannot move back any further; the
	// break goes after it instead of standing still.
	const std::vector<std::size_t> breaks = WrapColumns("\tabc", 2, 4);

	REQUIRE(!breaks.empty());
	CHECK(breaks[0] == 4);
}

TEST_CASE("A tab wider than the row at the end of a line does not add an empty row")
{
	// Columns: "bbbb" is 0-3 and the tab runs to column 8. Breaking after the tab would
	// start a row one past the end of the line.
	const std::vector<std::size_t> breaks = WrapColumns("bbbb\t", 3, 4);

	REQUIRE(!breaks.empty());
	CHECK(breaks.back() < ColumnCount("bbbb\t", 4));
}

TEST_CASE("A line of a single tab never breaks")
{
	CHECK(WrapColumns("\t", 1, 4).empty());
}

TEST_CASE("An offset on an unwrapped line maps to its own row and column")
{
	const TextBuffer buffer{std::string{"abc\ndef"}};

	RowMap rows;
	rows.Rebuild(buffer, 80, 4);

	const VisualPosition position = CaretToVisual(buffer, rows, 5, 4);

	CHECK(position.Row == 1);
	CHECK(position.Column == 1);
}

TEST_CASE("An offset past a break belongs to the following row")
{
	// One line of ten characters, four columns per row: rows start at columns 0, 4, 8.
	const TextBuffer buffer{std::string{"aaaaaaaaaa"}};

	RowMap rows;
	rows.Rebuild(buffer, 4, 4);

	// Column 4 is exactly a break: it is the first cell of row 1, never the last of row 0.
	const VisualPosition position = CaretToVisual(buffer, rows, 4, 4);

	CHECK(position.Row == 1);
	CHECK(position.Column == 0);
}

TEST_CASE("A column past the end of a row clamps to that row's last position")
{
	const TextBuffer buffer{std::string{"abc\ndef"}};

	RowMap unwrapped;
	unwrapped.Rebuild(buffer, 80, 4);

	// Clicking far to the right of a short line puts the caret at its end, which is what
	// the widget already does when it is not wrapping.
	CHECK(VisualToCaret(buffer, unwrapped, VisualPosition{0, 99}, 4) == 3);

	// Same idea for a row that is not the last one in its line: clicking past the end of
	// row 0 must land on its own last character (offset 3), not spill onto row 1's first
	// cell (offset 4). The row-end clamp has to agree with the break rule above, or a
	// click at the right edge of a wrapped row jumps to the wrong row.
	const TextBuffer wrapped{std::string{"aaaaaaaaaa"}};

	RowMap wrappedRows;
	wrappedRows.Rebuild(wrapped, 4, 4);

	CHECK(VisualToCaret(wrapped, wrappedRows, VisualPosition{0, 99}, 4) == 3);

	// A space-broken row: "alpha beta gamma" at width 6 breaks at columns 6 and 11, so
	// row 0 is "alpha " and its last caret position is column 5, just before the space
	// it broke on.
	const TextBuffer spaceWrapped{std::string{"alpha beta gamma"}};

	RowMap spaceWrappedRows;
	spaceWrappedRows.Rebuild(spaceWrapped, 6, 4);

	CHECK(VisualToCaret(spaceWrapped, spaceWrappedRows, VisualPosition{0, 99}, 4) == 5);

	// A middle row, where rowStart is not zero: row 1 spans columns 6 to 10.
	CHECK(VisualToCaret(spaceWrapped, spaceWrappedRows, VisualPosition{1, 99}, 4) == 10);
}

TEST_CASE("Both directions compose to the identity for every offset")
{
	// The property that matters: drawing, hit-testing and caret movement all go through
	// this pair, so any disagreement between them shows up here first.
	const TextBuffer buffer{std::string{"alpha beta gamma\n\tindented line\nx\n\nlast"}};

	RowMap rows;
	rows.Rebuild(buffer, 6, 4);

	for (std::size_t offset = 0; offset <= buffer.GetLength(); ++offset)
	{
		const VisualPosition position = CaretToVisual(buffer, rows, offset, 4);

		CAPTURE(offset);
		CHECK(VisualToCaret(buffer, rows, position, 4) == offset);
	}
}

TEST_CASE("Both directions compose to the identity across characters outside ASCII")
{
	// The case above walks every *byte*, which quietly restricts it to ASCII: an offset
	// inside a multi-byte character cannot round-trip, because no caret may sit there.
	// "A character outside ASCII is one column" is a rule of this project, and this pair
	// is where the drawing half and the hit-testing half would first be caught disagreeing
	// about it — so the same property is checked again over text that exercises the rule,
	// advancing by character boundaries instead of by bytes.
	const TextBuffer buffer{
		std::string{"zaz\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87 g\xC4\x99\xC5\x9Bl\xC4\x85 ja\xC5\xBA\xC5\x84"
					"\n\t\xC4\x85\xC4\x87\xC4\x99 wrapped\n\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC"}};

	RowMap rows;
	rows.Rebuild(buffer, 6, 4);

	const std::string text = buffer.Materialize();
	std::size_t offset = 0;

	while (true)
	{
		const VisualPosition position = CaretToVisual(buffer, rows, offset, 4);

		CAPTURE(offset);
		CHECK(VisualToCaret(buffer, rows, position, 4) == offset);

		if (offset >= text.size())
			break;

		offset = NextCharacter(text, offset);
	}
}

TEST_CASE("Wrapping a very long line stays linear")
{
	// A performance guard, not a behaviour test. WrapColumns used to probe columns through
	// ByteOfColumn, which walks the line from byte 0 on every call: one line cost
	// Theta(length^2 / width), measured at 7 seconds for 128 KB and four times that per
	// doubling. This line is 200 000 characters — a small minified script — and would take
	// upwards of fifteen seconds if that walk came back, with the UI thread blocked and no
	// way to cancel the frame. It runs in single-digit milliseconds while the walk stays
	// linear.
	//
	// The count is arithmetic, not a recorded number. Columns are 0..199 999, a space
	// every 400 columns at 399, 799, ... Inside each 400-column block the row limit is
	// reached three times with no space behind it (hard breaks at +120, +240, +360) and
	// the fourth row does find one, breaking just past it at the next multiple of 400.
	// That is four breaks per block for the first 499 blocks; the last block runs out of
	// line after its three hard breaks, because what remains after +360 fits in one row.
	std::string line;
	line.reserve(200000);

	for (std::size_t block = 0; block < 500; ++block)
	{
		line.append(399, 'a');
		line.push_back(' ');
	}

	REQUIRE(line.size() == 200000);

	const auto started = std::chrono::steady_clock::now();
	const std::vector<std::size_t> breaks = WrapColumns(line, 120, 4);
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
							 std::chrono::steady_clock::now() - started)
							 .count();

	CHECK(breaks.size() == 499 * 4 + 3);
	CHECK(breaks.front() == 120);
	CHECK(breaks[3] == 400);

	// Deliberately loose: this has to hold on a cold, unoptimised, instrumented Debug
	// build without ever being flaky, while still being three orders of magnitude below
	// what the quadratic walk cost.
	CHECK(elapsed < 2000);
}
