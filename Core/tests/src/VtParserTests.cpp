#include <doctest/doctest.h>

#include "Core/Term/TerminalScreen.h"
#include "Core/Term/VtParser.h"

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
}

TEST_CASE("VtParser prints plain text")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	parser.Consume("hello");

	CHECK(RowText(screen, 0) == U"hello   ");
}

TEST_CASE("VtParser places the cursor absolutely")
{
	Core::TerminalScreen screen(8, 4);
	Core::VtParser parser(screen);

	// CUP is one-based on the wire and zero-based in the screen.
	parser.Consume("\x1B[3;5Hx");

	CHECK(screen.At(2, 4).Code == U'x');
}

TEST_CASE("VtParser moves the cursor relatively")
{
	Core::TerminalScreen screen(8, 4);
	Core::VtParser parser(screen);

	parser.Consume("\x1B[2;2H\x1B[B\x1B[2Cx");

	CHECK(screen.At(2, 3).Code == U'x');
}

TEST_CASE("VtParser treats a missing parameter as one")
{
	Core::TerminalScreen screen(8, 4);
	Core::VtParser parser(screen);

	parser.Consume("\x1B[2;2H\x1B[Ax");

	CHECK(screen.At(0, 1).Code == U'x');
}

TEST_CASE("VtParser erases to the end of the line")
{
	Core::TerminalScreen screen(6, 2);
	Core::VtParser parser(screen);

	parser.Consume("abcdef\x1B[1;3H\x1B[K");

	CHECK(RowText(screen, 0) == U"ab    ");
}

TEST_CASE("VtParser erases the display")
{
	Core::TerminalScreen screen(4, 2);
	Core::VtParser parser(screen);

	parser.Consume("ab\x1B[2J");

	CHECK(RowText(screen, 0) == U"    ");
}

TEST_CASE("VtParser sets a scroll region and reverse-indexes inside it")
{
	Core::TerminalScreen screen(4, 4);
	Core::VtParser parser(screen);

	parser.Consume("\x1B[2;3r");
	parser.Consume("\x1B[2;1Hone\x1BM");

	CHECK(RowText(screen, 1) == U"    ");
	CHECK(RowText(screen, 2) == U"one ");
}

TEST_CASE("VtParser handles a sequence split across two reads")
{
	Core::TerminalScreen screen(8, 4);
	Core::VtParser parser(screen);

	// A pipe splits wherever it likes. A parser that resets between calls loses this.
	parser.Consume("\x1B[");
	parser.Consume("3;");
	parser.Consume("2Hx");

	CHECK(screen.At(2, 1).Code == U'x');
}

TEST_CASE("VtParser swallows an unknown sequence whole")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	parser.Consume("a\x1B[?99;7$pb");

	// Nothing of the sequence reaches the screen — not even its tail.
	CHECK(RowText(screen, 0) == U"ab      ");
}

TEST_CASE("VtParser swallows an operating system command and takes the title from it")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	parser.Consume("\x1B]0;git status\x07ok");

	CHECK(screen.GetTitle() == "git status");
	CHECK(RowText(screen, 0) == U"ok      ");
}

TEST_CASE("VtParser saves and restores the cursor")
{
	Core::TerminalScreen screen(8, 4);
	Core::VtParser parser(screen);

	parser.Consume("\x1B[3;3H\x1B""7\x1B[1;1H\x1B""8x");

	CHECK(screen.At(2, 2).Code == U'x');
}
