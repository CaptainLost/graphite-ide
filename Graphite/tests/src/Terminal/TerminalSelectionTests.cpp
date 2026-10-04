#include <doctest/doctest.h>

#include "Terminal/TerminalSelection.h"

#include "Core/Terminal/TerminalScreen.h"
#include "Core/Text/TextSelection.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace Graphite;

namespace {

	void PrintAscii(Core::TerminalScreen& screen, std::string_view text)
	{
		for (const char character : text)
		{
			screen.Print(static_cast<char32_t>(character), 1);
		}
	}

	void NewLine(Core::TerminalScreen& screen)
	{
		screen.CarriageReturn();
		screen.LineFeed();
	}

	// "a", a double-width character over two cells, "b", then the row's blank remainder.
	std::vector<Core::Cell> WideRow()
	{
		Core::TerminalScreen screen(6, 1);

		screen.Print(U'a', 1);
		screen.Print(U'界', 2);
		screen.Print(U'b', 1);

		std::vector<Core::Cell> cells;

		for (std::uint16_t column = 0; column < screen.GetColumns(); ++column)
		{
			cells.push_back(screen.At(0, column));
		}

		return cells;
	}

	std::vector<Core::Cell> AsciiRow(std::string_view text)
	{
		std::vector<Core::Cell> cells;

		for (const char character : text)
		{
			Core::Cell cell;
			cell.Code = static_cast<char32_t>(character);
			cells.push_back(cell);
		}

		return cells;
	}

	std::int64_t ScreenLine(const Core::TerminalScreen& screen, int row)
	{
		return LineOfScreenRow(screen.GetRowsScrolledOff(), row);
	}

	Core::TextSelection Between(Core::TextPoint from, Core::TextPoint to)
	{
		Core::TextSelection selection;
		selection.Select(from, to);
		return selection;
	}
}

TEST_CASE("TerminalSelection gives a row the same line on the screen and in the scrollback")
{
	// A row on screen row 3 with five rows scrolled off; four more scroll off and it sits at
	// the end of a hundred-line scrollback.
	CHECK(LineOfScreenRow(5, 3) == 8);
	CHECK(LineOfScrollbackRow(9, 100, 99) == 8);
	CHECK(LineOfScreenRow(9, -1) == 8);
}

TEST_CASE("TerminalSelection keeps a selection on its text while output scrolls")
{
	Core::TerminalScreen screen(10, 3);

	PrintAscii(screen, "alpha");
	NewLine(screen);
	PrintAscii(screen, "beta");

	const std::int64_t beta = ScreenLine(screen, 1);
	const Core::TextSelection selection = Between({beta, 0}, {beta, 4});

	REQUIRE(SelectedTerminalText(selection, screen, 10) == "beta");

	for (int line = 0; line < 5; ++line)
	{
		NewLine(screen);
		PrintAscii(screen, "more");
	}

	REQUIRE(screen.GetScrollbackCount() > 0);
	CHECK(SelectedTerminalText(selection, screen, 10) == "beta");
	CHECK_FALSE(SelectionLeftHistory(selection, screen.GetRowsScrolledOff(), screen.GetScrollbackCount()));
}

TEST_CASE("TerminalSelection knows a selection whose text has left the scrollback")
{
	Core::TerminalScreen screen(10, 2, 2);

	PrintAscii(screen, "first");
	const Core::TextSelection selection = Between({ScreenLine(screen, 0), 0}, {ScreenLine(screen, 0), 5});

	NewLine(screen);
	NewLine(screen);
	NewLine(screen);

	// Two rows scrolled off, both still held.
	CHECK_FALSE(SelectionLeftHistory(selection, screen.GetRowsScrolledOff(), screen.GetScrollbackCount()));
	CHECK(SelectedTerminalText(selection, screen, 10) == "first");

	NewLine(screen);

	CHECK(SelectionLeftHistory(selection, screen.GetRowsScrolledOff(), screen.GetScrollbackCount()));
	CHECK(SelectedTerminalText(selection, screen, 10).empty());
}

TEST_CASE("TerminalSelection treats the whole selection as gone once the scrollback is cleared")
{
	Core::TerminalScreen screen(10, 2);

	PrintAscii(screen, "old");
	NewLine(screen);
	NewLine(screen);

	const std::int64_t old = LineOfScrollbackRow(screen.GetRowsScrolledOff(), screen.GetScrollbackCount(), 0);
	const Core::TextSelection selection = Between({old, 0}, {old, 3});

	REQUIRE(SelectedTerminalText(selection, screen, 10) == "old");

	screen.ClearScrollback();

	CHECK(SelectionLeftHistory(selection, screen.GetRowsScrolledOff(), screen.GetScrollbackCount()));
}

TEST_CASE("TerminalSelection never says an empty selection left the history")
{
	CHECK_FALSE(SelectionLeftHistory(Core::TextSelection{}, 1000, 0));
}

TEST_CASE("TerminalSelection writes a double-width character once")
{
	const std::vector<Core::Cell> cells = WideRow();

	CHECK(TerminalRowText(cells, 4) == "a界" "b");
	CHECK(TerminalCellBytes(cells, 4) == std::vector<std::size_t>{0, 1, 4, 4, 5});
}

TEST_CASE("TerminalSelection gives cells the row does not have the end of its text")
{
	const std::vector<Core::Cell> cells = AsciiRow("ab");

	CHECK(TerminalRowText(cells, 4) == "ab");
	CHECK(TerminalCellBytes(cells, 4) == std::vector<std::size_t>{0, 1, 2, 2, 2});
}

TEST_CASE("TerminalSelection double-click takes a whole path with its line suffix")
{
	const std::vector<Core::Cell> cells = AsciiRow("open src/main.cpp:42 now");

	CHECK(TerminalWordAt(cells, cells.size(), 9) == Core::ColumnSpan{5, 20});
	CHECK(TerminalWordAt(cells, cells.size(), 5) == Core::ColumnSpan{5, 20});
	CHECK(TerminalWordAt(cells, cells.size(), 19) == Core::ColumnSpan{5, 20});
}

TEST_CASE("TerminalSelection double-click on a gap takes the one cell")
{
	const std::vector<Core::Cell> cells = AsciiRow("a  b");

	CHECK(TerminalWordAt(cells, cells.size(), 1) == Core::ColumnSpan{1, 2});
	CHECK(TerminalWordAt(cells, cells.size(), 9) == Core::ColumnSpan{4, 4});
}

TEST_CASE("TerminalSelection double-click counts a trailer with its leader")
{
	const std::vector<Core::Cell> cells = WideRow();

	// The whole of "a", the wide character and "b", from either half of the wide one.
	CHECK(TerminalWordAt(cells, 6, 1) == Core::ColumnSpan{0, 4});
	CHECK(TerminalWordAt(cells, 6, 2) == Core::ColumnSpan{0, 4});
}

TEST_CASE("TerminalSelection hit-tests to the nearest boundary and never inside a wide character")
{
	const std::vector<Core::Cell> cells = WideRow();

	CHECK(TerminalColumnAt(cells, 6, 0.4f) == 0);
	CHECK(TerminalColumnAt(cells, 6, 0.6f) == 1);
	// The boundary between the two halves of the wide character, approached from either side.
	CHECK(TerminalColumnAt(cells, 6, 1.9f) == 1);
	CHECK(TerminalColumnAt(cells, 6, 2.1f) == 3);
	CHECK(TerminalColumnAt(cells, 6, -3.0f) == 0);
	CHECK(TerminalColumnAt(cells, 6, 40.0f) == 6);
}

TEST_CASE("TerminalSelection names the cell under the pointer, a wide character by its leader")
{
	const std::vector<Core::Cell> cells = WideRow();

	CHECK(TerminalCellAt(cells, 6, 0.9f) == 0);
	CHECK(TerminalCellAt(cells, 6, 1.2f) == 1);
	// The right half of the wide character is still that character.
	CHECK(TerminalCellAt(cells, 6, 2.7f) == 1);
	CHECK(TerminalCellAt(cells, 6, 3.1f) == 3);
	CHECK(TerminalCellAt(cells, 6, -2.0f) == 0);
	CHECK(TerminalCellAt(cells, 6, 40.0f) == 6);
}

TEST_CASE("TerminalSelection double-click on the right half of a word's last character takes the word")
{
	Core::TerminalScreen screen(10, 1);

	PrintAscii(screen, "ls foo");

	TerminalGrid grid;
	grid.CellWidth = 10.0f;
	grid.CellHeight = 20.0f;
	grid.Columns = 10;
	grid.VisibleRows = 1;

	// The right half of the 's': the nearest boundary is after it, the cell under it is the 's'.
	const float x = 18.0f;
	const std::int64_t line = ScreenLine(screen, 0);

	REQUIRE(TerminalPointAt(screen, grid, x, 5.0f) == Core::TextPoint{line, 2});

	const std::optional<Core::TextPoint> character = TerminalCharacterAt(screen, grid, x, 5.0f);

	REQUIRE(character == Core::TextPoint{line, 1});

	std::vector<Core::Cell> scratch;
	const std::vector<Core::Cell>* cells = TerminalLineCells(screen, line, 10, scratch);

	REQUIRE(cells != nullptr);
	CHECK(TerminalWordAt(*cells, 10, character->Column) == Core::ColumnSpan{0, 2});
}

TEST_CASE("TerminalSelection copies a stream across rows, trimming each row's trailing spaces")
{
	Core::TerminalScreen screen(10, 4);

	PrintAscii(screen, "  one");
	NewLine(screen);
	NewLine(screen);
	PrintAscii(screen, "three");

	const Core::TextSelection selection =
		Between({ScreenLine(screen, 0), 1}, {ScreenLine(screen, 2), 3});

	// The leading space of the first row is kept; the blank middle row is an empty line.
	CHECK(SelectedTerminalText(selection, screen, 10) == " one\n\nthr");
}

TEST_CASE("TerminalSelection copies across the scrollback into the screen")
{
	Core::TerminalScreen screen(10, 2);

	PrintAscii(screen, "history");
	NewLine(screen);
	PrintAscii(screen, "middle");
	NewLine(screen);
	PrintAscii(screen, "now");

	REQUIRE(screen.GetScrollbackCount() == 1);

	const std::int64_t first =
		LineOfScrollbackRow(screen.GetRowsScrolledOff(), screen.GetScrollbackCount(), 0);
	const Core::TextSelection selection = Between({first, 4}, {ScreenLine(screen, 1), 10});

	CHECK(SelectedTerminalText(selection, screen, 10) == "ory\nmiddle\nnow");
}

TEST_CASE("TerminalSelection skips rows below the screen without a line break")
{
	Core::TerminalScreen screen(10, 2);

	PrintAscii(screen, "top");

	const Core::TextSelection selection = Between({ScreenLine(screen, 0), 0}, {ScreenLine(screen, 5), 4});

	CHECK(SelectedTerminalText(selection, screen, 10) == "top\n");
}

TEST_CASE("TerminalSelection copies a wide character half covered by the end of a selection once")
{
	Core::TerminalScreen screen(6, 1);

	screen.Print(U'a', 1);
	screen.Print(U'界', 2);
	screen.Print(U'b', 1);

	const std::int64_t line = ScreenLine(screen, 0);

	CHECK(SelectedTerminalText(Between({line, 0}, {line, 4}), screen, 6) == "a界" "b");
	CHECK(SelectedTerminalText(Between({line, 1}, {line, 3}), screen, 6) == "界");
}

TEST_CASE("TerminalSelection finds the point under the pointer among the rows shown")
{
	Core::TerminalScreen screen(10, 3);

	PrintAscii(screen, "zero");
	NewLine(screen);
	PrintAscii(screen, "one");
	NewLine(screen);
	PrintAscii(screen, "two");
	NewLine(screen);
	PrintAscii(screen, "three");

	REQUIRE(screen.GetScrollbackCount() == 1);

	TerminalGrid grid;
	grid.OriginX = 100.0f;
	grid.OriginY = 50.0f;
	grid.CellWidth = 10.0f;
	grid.CellHeight = 20.0f;
	grid.Columns = 10;
	grid.VisibleRows = 3;
	grid.ScrollOffset = 1; // "zero" from the scrollback at the top, "two" at the bottom

	const std::int64_t zero = LineOfScrollbackRow(screen.GetRowsScrolledOff(), 1, 0);

	CHECK(TerminalPointAt(screen, grid, 122.0f, 55.0f) == Core::TextPoint{zero, 2});
	CHECK(TerminalPointAt(screen, grid, 128.0f, 75.0f) == Core::TextPoint{zero + 1, 3});

	// Above the grid: the start of the first row shown. Below: the end of the last.
	CHECK(TerminalPointAt(screen, grid, 150.0f, 10.0f) == Core::TextPoint{zero, 0});
	CHECK(TerminalPointAt(screen, grid, 120.0f, 500.0f) == Core::TextPoint{zero + 2, 10});
}

TEST_CASE("TerminalSelection select all stops at the last row that holds anything")
{
	Core::TerminalScreen screen(10, 5);

	PrintAscii(screen, "gone");
	NewLine(screen);
	PrintAscii(screen, "prompt>");

	const auto [from, to] = TerminalEverything(screen, 10);

	CHECK(from == Core::TextPoint{ScreenLine(screen, 0), 0});
	CHECK(to == Core::TextPoint{ScreenLine(screen, 1), 10});

	Core::TextSelection selection;
	selection.Select(from, to);

	CHECK(SelectedTerminalText(selection, screen, 10) == "gone\nprompt>");
}

TEST_CASE("TerminalSelection select all starts at the oldest scrollback line")
{
	Core::TerminalScreen screen(10, 1);

	PrintAscii(screen, "old");
	NewLine(screen);
	PrintAscii(screen, "new");

	const auto [from, to] = TerminalEverything(screen, 10);

	CHECK(from == Core::TextPoint{LineOfScrollbackRow(screen.GetRowsScrolledOff(), 1, 0), 0});
	CHECK(to == Core::TextPoint{ScreenLine(screen, 0), 10});
}
