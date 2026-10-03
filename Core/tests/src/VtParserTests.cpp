#include <doctest/doctest.h>

#include "Core/Term/TerminalScreen.h"
#include "Core/Term/VtParser.h"
#include "Core/Text/Utf8.h"

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

	parser.Consume("\x1B[3;3H\x1B"
				   "7\x1B[1;1H\x1B"
				   "8x");

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

	// modifyOtherKeys is emitted by real programs; its final 'm' must not reach the screen
	// as text, and must not be dispatched as an SGR sequence either.
	const std::string reply = parser.Consume("a\x1B[>4;2mb");

	CHECK(RowText(screen, 0) == U"ab      ");
	CHECK(reply.empty());
}

TEST_CASE("VtParser answers a secondary device attributes request")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	// tmux and some ncurses probes send this and wait on a timeout for an answer.
	CHECK(parser.Consume("\x1B[>0c") == "\x1B[>0;10;1c");
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
	parser.Consume("\x1B]0;" + std::string(65536, 'x') + "\x07" + "ok");

	CHECK(screen.GetTitle().size() < 65536);
	CHECK(RowText(screen, 0) == U"ok      ");
}

TEST_CASE("VtParser drops a code that falls past the parameter cap")
{
	Core::TerminalScreen screen(4, 2);
	Core::VtParser parser(screen);

	// Fifty "0;" parameters followed by "1" for bold. If the cap did not exist, the trailing
	// "1" would land in the parameter list and set Bold; capped at 32, that digit is dropped
	// along with everything past it, so only leading zeros (which reset the pen) are seen.
	std::string sequence = "\x1B[";
	for (int i = 0; i < 49; ++i)
	{
		sequence += "0;";
	}
	sequence += "1ma";

	parser.Consume(sequence);

	CHECK_FALSE(Core::HasFlag(screen.At(0, 0).Flags, Core::CellFlags::Bold));
}

TEST_CASE("VtParser caps the intermediate byte buffer")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	// A run of 0x20-0x2F bytes with no final never terminates the sequence; the buffer must
	// not grow with it. The next byte is a final this parser does not implement with any
	// intermediate present, so nothing reaches the screen and no crash or unbounded growth
	// occurs either way — the point is that the previous byte stream is bounded.
	parser.Consume("\x1B[" + std::string(4096, ' ') + "@");
	parser.Consume("ok");

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

TEST_CASE("VtParser tracks mouse reporting while any of its three modes is on")
{
	Core::TerminalScreen screen(4, 2);
	Core::VtParser parser(screen);

	REQUIRE_FALSE(screen.IsMouseReporting());

	// What Claude Code sends on start: all three at once, then the SGR encoding.
	parser.Consume("[?1000h[?1002h[?1003h[?1006h");
	CHECK(screen.IsMouseReporting());
	CHECK(screen.IsSgrMouse());

	// Motion off leaves clicks and drags reported.
	parser.Consume("[?1003l");
	CHECK(screen.IsMouseReporting());

	parser.Consume("[?1000l[?1002l[?1006l");
	CHECK_FALSE(screen.IsMouseReporting());
	CHECK_FALSE(screen.IsSgrMouse());
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

TEST_CASE("VtParser decodes a multi-byte character")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	parser.Consume("\xC5\x82"); // U+0142, a Polish l with stroke

	CHECK(screen.At(0, 0).Code == U'\u0142');
	CHECK(screen.GetCursor().Column == 1);
}

TEST_CASE("VtParser decodes a character split across two reads")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	parser.Consume("\xC5");
	parser.Consume("\x82");

	CHECK(screen.At(0, 0).Code == U'\u0142');
}

TEST_CASE("VtParser gives a double-width character two cells")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	parser.Consume("\xE4\xBD\xA0"); // U+4F60

	CHECK(screen.At(0, 0).Code == U'\u4F60');
	CHECK(screen.At(0, 1).WideTrailer);
	CHECK(screen.GetCursor().Column == 2);
}

TEST_CASE("VtParser replaces an invalid byte rather than dropping the stream")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	parser.Consume("a\xFF"
				   "b");

	CHECK(screen.At(0, 0).Code == U'a');
	CHECK(screen.At(0, 2).Code == U'b');
}

TEST_CASE("VtParser re-reads the byte that broke a multi-byte character")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	// 0xC5 opens a two-byte character but 'Z' is not a continuation byte: the
	// character is broken, and 'Z' must still be read as a fresh character rather
	// than being consumed as though it were part of the ruined one.
	parser.Consume("\xC5"
				   "Z");

	CHECK(screen.At(0, 0).Code == U'\xFFFD');
	CHECK(screen.At(0, 1).Code == U'Z');
	CHECK(screen.GetCursor().Column == 2);
}

TEST_CASE("VtParser re-reads a byte that breaks a multi-byte character across reads")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	// Same recovery as above, but the lead byte and the offending byte arrive in
	// separate Consume calls: the pending-character state must survive the call
	// boundary and still recover the same way.
	parser.Consume("\xC5");
	parser.Consume("Z");

	CHECK(screen.At(0, 0).Code == U'\xFFFD');
	CHECK(screen.At(0, 1).Code == U'Z');
	CHECK(screen.GetCursor().Column == 2);
}

TEST_CASE("VtParser does not decode an overlong form into the character it disguises")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	// C0 AF is '/' in two bytes. C0 can never lead, and AF then has nothing to continue.
	parser.Consume("\xC0\xAF"
				   "b");

	CHECK(screen.At(0, 0).Code == Core::kReplacementCharacter);
	CHECK(screen.At(0, 1).Code == Core::kReplacementCharacter);
	CHECK(screen.At(0, 2).Code == U'b');
}

TEST_CASE("VtParser does not decode an encoded surrogate")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	// ED A0 80 would be U+D800. ED's sequence breaks at A0, which is then read again and
	// is a continuation byte with nothing to continue, like the 80 after it.
	parser.Consume("\xED\xA0\x80"
				   "b");

	CHECK(screen.At(0, 0).Code == Core::kReplacementCharacter);
	CHECK(screen.At(0, 1).Code == Core::kReplacementCharacter);
	CHECK(screen.At(0, 2).Code == Core::kReplacementCharacter);
	CHECK(screen.At(0, 3).Code == U'b');
}

TEST_CASE("VtParser does not decode a value above U+10FFFF")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	parser.Consume("\xF4\x90\x80\x80"
				   "b");

	CHECK(screen.At(0, 0).Code == Core::kReplacementCharacter);
	CHECK(screen.At(0, 3).Code == Core::kReplacementCharacter);
	CHECK(screen.At(0, 4).Code == U'b');
}

TEST_CASE("VtParser replaces a continuation byte that arrives with nothing pending")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	// 0x82 is only ever valid as a continuation byte; with no character in
	// progress it is malformed on its own and must not be folded into 'a'.
	parser.Consume("a"
				   "\x82"
				   "b");

	CHECK(screen.At(0, 0).Code == U'a');
	CHECK(screen.At(0, 1).Code == U'\xFFFD');
	CHECK(screen.At(0, 2).Code == U'b');
}

TEST_CASE("VtParser swallows a character-set selection whole")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	// "ESC ( B" selects the ASCII character set. It is not implemented, but its final byte
	// must not reach the screen: ncurses emits "\E(B\E[m" as sgr0 on essentially every
	// attribute reset, so a leaked 'B' would litter the screen of anything that uses colour.
	parser.Consume("\x1B(B"
				   "hi");

	CHECK(RowText(screen, 0) == U"hi      ");
	CHECK(screen.GetCursor().Column == 2);
}

TEST_CASE("VtParser swallows the line-drawing character set and a DEC alignment test")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	// The same rule for the other intermediates real programs emit: "ESC ( 0" switches to the
	// line-drawing set and "ESC # 8" is the DEC screen alignment test.
	parser.Consume("\x1B(0"
				   "a");
	parser.Consume("\x1B#8"
				   "b");

	CHECK(RowText(screen, 0) == U"ab      ");
	CHECK(screen.GetCursor().Column == 2);
}

TEST_CASE("VtParser clamps a repetition count to the screen")
{
	Core::TerminalScreen screen(8, 5);
	Core::VtParser parser(screen);

	parser.Consume("hello");

	// The digit accumulator caps a parameter at 65535, so an unclamped scroll would copy
	// hundreds of millions of cells and push tens of thousands of blank lines into the
	// scrollback. Only the rows that exist can leave the screen.
	parser.Consume("\x1B[65535S");

	CHECK(screen.GetScrollbackCount() == 5);
	CHECK(RowText(screen, 0) == U"        ");
	CHECK(screen.GetRows() == 5);
	CHECK(screen.GetColumns() == 8);

	parser.Consume("\x1B[65535L\x1B[65535M\x1B[65535@\x1B[65535P\x1B[65535T");

	CHECK(screen.GetRows() == 5);
	CHECK(screen.GetColumns() == 8);
	CHECK(RowText(screen, 0) == U"        ");
}

TEST_CASE("VtParser drops a CSI with a colon sub-parameter whole")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	// The sequence is dropped rather than interpreted, but its tail must not print.
	parser.Consume("\x1B[4:3mX");

	CHECK(RowText(screen, 0) == U"X       ");
}

TEST_CASE("VtParser drops a colon-separated true-colour sequence whole")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	parser.Consume("\x1B[38:2::1:2:3mX");

	CHECK(RowText(screen, 0) == U"X       ");
}

TEST_CASE("VtParser drops a CSI with a private byte after the first parameter byte")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	parser.Consume("\x1B[1?2mX");

	CHECK(RowText(screen, 0) == U"X       ");
}

TEST_CASE("VtParser resumes normally after a dropped CSI")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	parser.Consume("\x1B[4:3m\x1B[2;3Hx");

	CHECK(screen.At(1, 2).Code == U'x');
}

TEST_CASE("VtParser ignores DEL in the ground state")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	parser.Consume("a\x7F"
				   "b");

	CHECK(RowText(screen, 0) == U"ab      ");
	CHECK(screen.GetCursor().Column == 2);
}

TEST_CASE("VtParser RIS restores every mode the screen tracks")
{
	Core::TerminalScreen screen(8, 6);
	Core::VtParser parser(screen);

	parser.Consume("main");
	parser.Consume("\x1B[?1049h\x1B[2;4r\x1B[?7l\x1B[?2004h\x1B[?25l\x1B[?1000h\x1B[?1006h");

	REQUIRE(screen.IsAlternateBuffer());
	REQUIRE_FALSE(screen.IsAutoWrap());
	REQUIRE(screen.IsBracketedPaste());
	REQUIRE_FALSE(screen.IsCursorVisible());
	REQUIRE(screen.IsMouseReporting());
	REQUIRE(screen.IsSgrMouse());

	parser.Consume("\x1B"
				   "c");

	CHECK_FALSE(screen.IsAlternateBuffer());
	CHECK(screen.IsAutoWrap());
	CHECK_FALSE(screen.IsBracketedPaste());
	CHECK(screen.IsCursorVisible());
	CHECK_FALSE(screen.IsMouseReporting());
	CHECK_FALSE(screen.IsSgrMouse());

	// A full-height scroll region again: a line feed on the last row scrolls into history.
	parser.Consume("\x1B[6;1H\n");
	CHECK(screen.GetScrollbackCount() == 1);
	CHECK(RowText(screen, 0) == U"        ");
}

TEST_CASE("VtParser abandons an ignored CSI on a control byte")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	// CAN cancels the sequence, as it does in the states that interpret one.
	parser.Consume("\x1B[4:\x18"
				   "abc");

	CHECK(RowText(screen, 0) == U"abc     ");
}

TEST_CASE("VtParser RIS forgets the saved cursor")
{
	Core::TerminalScreen screen(8, 4);
	Core::VtParser parser(screen);

	parser.Consume("\x1B[3;5H\x1B"
				   "7\x1B[1;1H\x1B"
				   "c\x1B"
				   "8");

	CHECK(screen.GetCursor().Row == 0);
	CHECK(screen.GetCursor().Column == 0);
}
