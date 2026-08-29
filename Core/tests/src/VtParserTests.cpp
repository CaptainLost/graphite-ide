#include <doctest/doctest.h>

#include "Core/Term/TerminalScreen.h"
#include "Core/Term/VtParser.h"

#include <string>

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

TEST_CASE("VtParser lets an ESC abort the sequence in progress")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	// The first sequence is abandoned halfway. Its ESC must start the second one rather
	// than being eaten as the abandoned sequence's unrecognised byte.
	parser.Consume("\x1B[3\x1B[2Jab");

	CHECK(RowText(screen, 0) == U"ab      ");
}

TEST_CASE("VtParser starts a fresh sequence on a doubled ESC")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	parser.Consume("\x1B\x1B[1;3Hx");

	CHECK(RowText(screen, 0) == U"  x     ");
}

TEST_CASE("VtParser swallows a CSI opened with a prefix other than '?'")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	// Secondary device attributes and modifyOtherKeys are emitted by real programs; the
	// finals 'c' and 'm' must not reach the screen, and must not be dispatched either.
	const std::string reply = parser.Consume("a\x1B[>0c\x1B[>4;2mb");

	CHECK(RowText(screen, 0) == U"ab      ");
	CHECK(reply.empty());
}

TEST_CASE("VtParser keeps a leading empty parameter")
{
	Core::TerminalScreen screen(8, 4);
	Core::VtParser parser(screen);

	// "ESC[;5H" is row default, column five — not column default, row five.
	parser.Consume("\x1B[;5Hx");

	CHECK(screen.At(0, 4).Code == U'x');
}

TEST_CASE("VtParser ignores a CSI carrying an intermediate byte it does not implement")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	// "CSI Ps SP @" is scroll left, not insert characters. Unimplemented means nothing
	// happens, never a different action.
	parser.Consume("abc\x1B[1;1H\x1B[1 @");

	CHECK(RowText(screen, 0) == U"abc     ");
}

TEST_CASE("VtParser drops a title whose sequence an ESC abandons")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	parser.Consume("\x1B]0;first\x07");
	parser.Consume("\x1B]0;second\x1B[2Jok");

	CHECK(screen.GetTitle() == "first");
	CHECK(RowText(screen, 0) == U"ok      ");
}

TEST_CASE("VtParser takes a title from a string-terminated operating system command")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	parser.Consume("\x1B]2;built\x1B\\ok");

	CHECK(screen.GetTitle() == "built");
	CHECK(RowText(screen, 0) == U"ok      ");
}

TEST_CASE("VtParser answers a cursor position report")
{
	Core::TerminalScreen screen(8, 4);
	Core::VtParser parser(screen);

	parser.Consume("\x1B[3;5H");

	// The reply is one-based, like the CUP that put the cursor there.
	CHECK(parser.Consume("\x1B[6n") == "\x1B[3;5R");
}

TEST_CASE("VtParser answers a device attributes request")
{
	Core::TerminalScreen screen(8, 4);
	Core::VtParser parser(screen);

	CHECK(parser.Consume("\x1B[c") == "\x1B[?1;2c");
	CHECK(parser.Consume("\x1B[0c") == "\x1B[?1;2c");
}

TEST_CASE("VtParser caps the parameter list and the operating system payload")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	// Untrusted bytes: neither buffer may grow with the stream.
	parser.Consume("\x1B[" + std::string(4096, ';') + "H");
	parser.Consume("\x1B]0;" + std::string(65536, 'x') + "\x07"
				   + "ok");

	CHECK(screen.GetTitle().size() < 65536);
	CHECK(RowText(screen, 0) == U"ok      ");
}

TEST_CASE("VtParser applies a palette foreground colour")
{
	Core::TerminalScreen screen(4, 2);
	Core::VtParser parser(screen);

	parser.Consume("\x1B[31ma");

	CHECK(screen.At(0, 0).Foreground.Kind == Core::TerminalColorKind::Palette);
	CHECK(screen.At(0, 0).Foreground.Index == 1);
}

TEST_CASE("VtParser applies a bright palette colour")
{
	Core::TerminalScreen screen(4, 2);
	Core::VtParser parser(screen);

	parser.Consume("\x1B[92ma");

	CHECK(screen.At(0, 0).Foreground.Index == 10);
}

TEST_CASE("VtParser applies a 256-colour index")
{
	Core::TerminalScreen screen(4, 2);
	Core::VtParser parser(screen);

	parser.Consume("\x1B[38;5;208ma");

	CHECK(screen.At(0, 0).Foreground.Kind == Core::TerminalColorKind::Palette);
	CHECK(screen.At(0, 0).Foreground.Index == 208);
}

TEST_CASE("VtParser applies a direct RGB colour")
{
	Core::TerminalScreen screen(4, 2);
	Core::VtParser parser(screen);

	parser.Consume("\x1B[38;2;10;20;30ma");

	CHECK(screen.At(0, 0).Foreground.Kind == Core::TerminalColorKind::Rgb);
	CHECK(screen.At(0, 0).Foreground.R == 10);
	CHECK(screen.At(0, 0).Foreground.G == 20);
	CHECK(screen.At(0, 0).Foreground.B == 30);
}

TEST_CASE("VtParser does not read past a truncated extended-colour sequence")
{
	Core::TerminalScreen screen(4, 2);
	Core::VtParser parser(screen);

	// "38;5" with no index following must not crash or read out of bounds.
	parser.Consume("\x1B[38;5ma");

	CHECK(screen.At(0, 0).Foreground.Kind == Core::TerminalColorKind::Default);
}

TEST_CASE("VtParser sets and clears attributes")
{
	Core::TerminalScreen screen(4, 2);
	Core::VtParser parser(screen);

	parser.Consume("\x1B[1;4ma\x1B[22mb");

	CHECK(Core::HasFlag(screen.At(0, 0).Flags, Core::CellFlags::Bold));
	CHECK(Core::HasFlag(screen.At(0, 0).Flags, Core::CellFlags::Underline));
	CHECK_FALSE(Core::HasFlag(screen.At(0, 1).Flags, Core::CellFlags::Bold));
	CHECK(Core::HasFlag(screen.At(0, 1).Flags, Core::CellFlags::Underline));
}

TEST_CASE("VtParser turns off both bold and faint with a single code")
{
	Core::TerminalScreen screen(4, 2);
	Core::VtParser parser(screen);

	parser.Consume("\x1B[1;2ma\x1B[22mb");

	CHECK(Core::HasFlag(screen.At(0, 0).Flags, Core::CellFlags::Bold));
	CHECK(Core::HasFlag(screen.At(0, 0).Flags, Core::CellFlags::Faint));
	CHECK_FALSE(Core::HasFlag(screen.At(0, 1).Flags, Core::CellFlags::Bold));
	CHECK_FALSE(Core::HasFlag(screen.At(0, 1).Flags, Core::CellFlags::Faint));
}

TEST_CASE("VtParser resets the pen")
{
	Core::TerminalScreen screen(4, 2);
	Core::VtParser parser(screen);

	parser.Consume("\x1B[31;1m\x1B[0ma");

	CHECK(screen.At(0, 0).Foreground.Kind == Core::TerminalColorKind::Default);
	CHECK(screen.At(0, 0).Flags == Core::CellFlags::None);
}

TEST_CASE("VtParser switches to the alternate buffer and back")
{
	Core::TerminalScreen screen(4, 2);
	Core::VtParser parser(screen);

	parser.Consume("keep");
	parser.Consume("\x1B[?1049h");

	CHECK(screen.IsAlternateBuffer());

	parser.Consume("\x1B[?1049l");

	CHECK_FALSE(screen.IsAlternateBuffer());
	CHECK(screen.At(0, 0).Code == U'k');
}

TEST_CASE("VtParser hides and shows the cursor")
{
	Core::TerminalScreen screen(4, 2);
	Core::VtParser parser(screen);

	parser.Consume("\x1B[?25l");
	CHECK_FALSE(screen.IsCursorVisible());

	parser.Consume("\x1B[?25h");
	CHECK(screen.IsCursorVisible());
}

TEST_CASE("VtParser turns bracketed paste on and off")
{
	Core::TerminalScreen screen(4, 2);
	Core::VtParser parser(screen);

	parser.Consume("\x1B[?2004h");
	CHECK(screen.IsBracketedPaste());

	parser.Consume("\x1B[?2004l");
	CHECK_FALSE(screen.IsBracketedPaste());
}

TEST_CASE("VtParser ignores a public mode it does not implement")
{
	Core::TerminalScreen screen(4, 2);
	Core::VtParser parser(screen);

	// Insert mode (4, unprefixed) is not a DEC private mode; it must be swallowed rather
	// than mistaken for a private one and acted on.
	parser.Consume("\x1B[4h");

	CHECK_FALSE(screen.IsAlternateBuffer());
	CHECK(screen.IsCursorVisible());
}
