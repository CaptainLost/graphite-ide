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
