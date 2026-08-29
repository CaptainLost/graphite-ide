#include <doctest/doctest.h>

#include "Core/Term/TerminalScreen.h"

#include <string_view>

namespace {

	std::u32string RowText(const Core::TerminalScreen& screen, std::uint16_t row)
	{
		std::u32string text;

		for (std::uint16_t column = 0; column < screen.GetColumns(); ++column)
		{
			text.push_back(screen.At(row, column).Code);
		}

		return text;
	}

	void PrintAscii(Core::TerminalScreen& screen, std::string_view text)
	{
		for (const char character : text)
		{
			screen.Print(static_cast<char32_t>(character), 1);
		}
	}
}

TEST_CASE("TerminalScreen starts blank with the cursor at the origin")
{
	Core::TerminalScreen screen(4, 2);

	CHECK(RowText(screen, 0) == U"    ");
	CHECK(screen.GetCursor().Row == 0);
	CHECK(screen.GetCursor().Column == 0);
}

TEST_CASE("TerminalScreen prints characters and advances the cursor")
{
	Core::TerminalScreen screen(4, 2);

	PrintAscii(screen, "ab");

	CHECK(RowText(screen, 0) == U"ab  ");
	CHECK(screen.GetCursor().Column == 2);
}

TEST_CASE("TerminalScreen defers the wrap until the character after the last column")
{
	Core::TerminalScreen screen(2, 2);

	PrintAscii(screen, "ab");

	// The cursor stays on the last column with a wrap pending: a program that now sends a
	// carriage return expects to overwrite this line, not the next one.
	CHECK(screen.GetCursor().Row == 0);
	CHECK(screen.GetCursor().Column == 1);

	PrintAscii(screen, "c");

	CHECK(screen.GetCursor().Row == 1);
	CHECK(screen.GetCursor().Column == 1);
	CHECK(RowText(screen, 0) == U"ab");
	CHECK(RowText(screen, 1) == U"c ");
}

TEST_CASE("TerminalScreen cancels a pending wrap on a carriage return")
{
	Core::TerminalScreen screen(2, 2);

	PrintAscii(screen, "ab");
	screen.CarriageReturn();
	PrintAscii(screen, "c");

	CHECK(RowText(screen, 0) == U"cb");
	CHECK(RowText(screen, 1) == U"  ");
}

TEST_CASE("TerminalScreen overwrites a line in place after a carriage return")
{
	Core::TerminalScreen screen(8, 2);

	PrintAscii(screen, "50%");
	screen.CarriageReturn();
	PrintAscii(screen, "75%");

	CHECK(RowText(screen, 0) == U"75%     ");
}

TEST_CASE("TerminalScreen moves down a line without changing the column on a line feed")
{
	Core::TerminalScreen screen(4, 3);

	PrintAscii(screen, "ab");
	screen.LineFeed();

	CHECK(screen.GetCursor().Row == 1);
	CHECK(screen.GetCursor().Column == 2);
}

TEST_CASE("TerminalScreen backspace steps left and stops at the margin")
{
	Core::TerminalScreen screen(4, 2);

	PrintAscii(screen, "ab");
	screen.Backspace();
	screen.Backspace();
	screen.Backspace();

	CHECK(screen.GetCursor().Column == 0);
}

TEST_CASE("TerminalScreen tab advances to the next eight-column stop")
{
	Core::TerminalScreen screen(24, 2);

	PrintAscii(screen, "ab");
	screen.Tab();

	CHECK(screen.GetCursor().Column == 8);

	screen.Tab();

	CHECK(screen.GetCursor().Column == 16);
}

TEST_CASE("TerminalScreen paints printed cells with the current pen")
{
	Core::TerminalScreen screen(4, 2);

	Core::Pen pen;
	pen.Foreground = Core::TerminalColor::FromPalette(1);
	pen.Flags = Core::CellFlags::Bold;
	screen.SetPen(pen);

	PrintAscii(screen, "a");

	CHECK(screen.At(0, 0).Foreground.Kind == Core::TerminalColorKind::Palette);
	CHECK(screen.At(0, 0).Foreground.Index == 1);
	CHECK(Core::HasFlag(screen.At(0, 0).Flags, Core::CellFlags::Bold));
}

TEST_CASE("TerminalScreen gives a double-width character two cells")
{
	Core::TerminalScreen screen(4, 2);

	screen.Print(U'你', 2);

	CHECK(screen.At(0, 0).Code == U'你');
	CHECK(screen.At(0, 1).WideTrailer);
	CHECK(screen.GetCursor().Column == 2);
}

TEST_CASE("TerminalScreen restores a saved cursor")
{
	Core::TerminalScreen screen(8, 4);

	screen.SetCursor(2, 3);
	screen.SaveCursor();
	screen.SetCursor(0, 0);
	screen.RestoreCursor();

	CHECK(screen.GetCursor().Row == 2);
	CHECK(screen.GetCursor().Column == 3);
}

TEST_CASE("TerminalScreen clamps a cursor placed outside the grid")
{
	Core::TerminalScreen screen(4, 2);

	screen.SetCursor(9, 9);

	CHECK(screen.GetCursor().Row == 1);
	CHECK(screen.GetCursor().Column == 3);
}

TEST_CASE("TerminalScreen wraps a double-width character whole instead of splitting it at the margin")
{
	Core::TerminalScreen screen(3, 2);

	// Two single-width characters leave exactly one column free; a double-width character
	// cannot split across the margin, so it must wrap to the next row whole.
	PrintAscii(screen, "ab");
	screen.Print(U'你', 2);

	CHECK(RowText(screen, 0) == U"ab ");
	CHECK(screen.At(1, 0).Code == U'你');
	CHECK(screen.At(1, 1).WideTrailer);
	CHECK(screen.GetCursor().Row == 1);
	CHECK(screen.GetCursor().Column == 2);
}

TEST_CASE("TerminalScreen erases from the cursor to the end of the line")
{
	Core::TerminalScreen screen(6, 2);

	PrintAscii(screen, "abcdef");
	screen.SetCursor(0, 2);
	screen.EraseInLine(Core::EraseExtent::ToEnd);

	CHECK(RowText(screen, 0) == U"ab    ");
}

TEST_CASE("TerminalScreen erases from the start of the line through the cursor")
{
	Core::TerminalScreen screen(6, 2);

	PrintAscii(screen, "abcdef");
	screen.SetCursor(0, 2);
	screen.EraseInLine(Core::EraseExtent::ToStart);

	// The boundary is cursor-inclusive: the cell under the cursor (index 2, 'c') is erased
	// along with everything to its left, and only what follows the cursor survives.
	CHECK(RowText(screen, 0) == U"   def");
}

TEST_CASE("TerminalScreen erases the whole display")
{
	Core::TerminalScreen screen(4, 2);

	PrintAscii(screen, "ab");
	screen.LineFeed();
	screen.CarriageReturn();
	PrintAscii(screen, "cd");
	screen.EraseInDisplay(Core::EraseExtent::All);

	CHECK(RowText(screen, 0) == U"    ");
	CHECK(RowText(screen, 1) == U"    ");
}

TEST_CASE("TerminalScreen erases from the cursor to the end of the display")
{
	Core::TerminalScreen screen(4, 3);

	PrintAscii(screen, "abcd");
	screen.CarriageReturn();
	screen.LineFeed();
	PrintAscii(screen, "efgh");
	screen.CarriageReturn();
	screen.LineFeed();
	PrintAscii(screen, "ijkl");

	screen.SetCursor(1, 2);
	screen.EraseInDisplay(Core::EraseExtent::ToEnd);

	// A row above the cursor is untouched; on the cursor's own row everything left of it
	// survives while the cursor cell and the rest of the row are erased; rows below are wiped.
	CHECK(RowText(screen, 0) == U"abcd");
	CHECK(RowText(screen, 1) == U"ef  ");
	CHECK(RowText(screen, 2) == U"    ");
}

TEST_CASE("TerminalScreen erases from the start of the display through the cursor")
{
	Core::TerminalScreen screen(4, 3);

	PrintAscii(screen, "abcd");
	screen.CarriageReturn();
	screen.LineFeed();
	PrintAscii(screen, "efgh");
	screen.CarriageReturn();
	screen.LineFeed();
	PrintAscii(screen, "ijkl");

	screen.SetCursor(1, 2);
	screen.EraseInDisplay(Core::EraseExtent::ToStart);

	// The mirror image of the ToEnd case: rows above the cursor are wiped, the cursor's own
	// row is cleared cursor-inclusive so only what follows it survives, and rows below are
	// untouched.
	CHECK(RowText(screen, 0) == U"    ");
	CHECK(RowText(screen, 1) == U"   h");
	CHECK(RowText(screen, 2) == U"ijkl");
}

TEST_CASE("TerminalScreen deletes characters and pulls the rest of the line left")
{
	Core::TerminalScreen screen(6, 1);

	PrintAscii(screen, "abcdef");
	screen.SetCursor(0, 1);
	screen.DeleteCharacters(2);

	CHECK(RowText(screen, 0) == U"adef  ");
}

TEST_CASE("TerminalScreen inserts characters and pushes the rest of the line right")
{
	Core::TerminalScreen screen(6, 1);

	PrintAscii(screen, "abcdef");
	screen.SetCursor(0, 1);
	screen.InsertCharacters(2);

	CHECK(RowText(screen, 0) == U"a  bcd");
}

TEST_CASE("TerminalScreen scrolling off the top fills the scrollback")
{
	Core::TerminalScreen screen(5, 2);

	PrintAscii(screen, "one");
	screen.LineFeed();
	screen.CarriageReturn();
	PrintAscii(screen, "two");
	screen.LineFeed();
	screen.CarriageReturn();
	PrintAscii(screen, "three");

	REQUIRE(screen.GetScrollbackCount() == 1);
	CHECK(screen.GetScrollbackLine(0)[0].Code == U'o');
	CHECK(RowText(screen, 0) == U"two  ");
}

TEST_CASE("TerminalScreen keeps the scrollback within its limit")
{
	Core::TerminalScreen screen(4, 1, 2);

	for (int line = 0; line < 6; ++line)
	{
		screen.LineFeed();
	}

	CHECK(screen.GetScrollbackCount() == 2);
}

TEST_CASE("TerminalScreen scrolls only inside the scroll region")
{
	Core::TerminalScreen screen(4, 4);

	screen.SetCursor(0, 0);
	PrintAscii(screen, "top");
	screen.SetScrollRegion(1, 2);
	screen.SetCursor(2, 0);
	PrintAscii(screen, "mid");
	screen.LineFeed();

	// The region scrolled; the line above it did not move.
	CHECK(RowText(screen, 0) == U"top ");
	CHECK(RowText(screen, 1) == U"mid ");

	// The line pushed out of this region (top row 1, not row 0) is a redraw artefact, not
	// scrollback history: a user's terminal history must not fill with debris every time a
	// full-screen program that uses scroll regions, such as vim, redraws and quits.
	CHECK(screen.GetScrollbackCount() == 0);
}

TEST_CASE("TerminalScreen reverse index scrolls down at the top of the region")
{
	Core::TerminalScreen screen(4, 3);

	screen.SetCursor(0, 0);
	PrintAscii(screen, "one");
	screen.SetCursor(0, 0);
	screen.ReverseIndex();

	CHECK(RowText(screen, 0) == U"    ");
	CHECK(RowText(screen, 1) == U"one ");
}

TEST_CASE("TerminalScreen leaving the alternate buffer restores the primary untouched")
{
	Core::TerminalScreen screen(4, 2);

	PrintAscii(screen, "keep");
	// A position distinct from where "editor stuff" leaves the cursor in the alternate
	// buffer, so restoring the wrong cursor would be caught rather than coincide.
	screen.SetCursor(1, 2);

	screen.UseAlternateBuffer(true);
	CHECK(RowText(screen, 0) == U"    ");
	screen.SetCursor(0, 0);
	PrintAscii(screen, "editor stuff");

	screen.UseAlternateBuffer(false);

	CHECK(RowText(screen, 0) == U"keep");
	CHECK(screen.GetCursor().Row == 1);
	CHECK(screen.GetCursor().Column == 2);
}

TEST_CASE("TerminalScreen alternate buffer does not feed the scrollback")
{
	Core::TerminalScreen screen(4, 1);

	screen.UseAlternateBuffer(true);

	for (int line = 0; line < 4; ++line)
	{
		screen.LineFeed();
	}

	CHECK(screen.GetScrollbackCount() == 0);
}

TEST_CASE("TerminalScreen resize truncates and pads without reflowing")
{
	Core::TerminalScreen screen(6, 2);

	PrintAscii(screen, "abcdef");
	screen.Resize(3, 2);

	CHECK(screen.GetColumns() == 3);
	CHECK(RowText(screen, 0) == U"abc");

	screen.Resize(5, 2);

	CHECK(RowText(screen, 0) == U"abc  ");
}
