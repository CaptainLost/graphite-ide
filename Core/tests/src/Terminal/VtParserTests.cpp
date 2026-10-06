#include <doctest/doctest.h>

#include "Core/Terminal/TerminalScreen.h"
#include "Core/Terminal/VtParser.h"
#include "Core/Text/Utf8.h"

#include "TerminalFixtures.h"

#include <string>
#include <string_view>

TEST_CASE("VtParser prints plain text")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	parser.Consume("hello");

	CHECK(ScreenRowText(screen, 0) == U"hello   ");
}

TEST_CASE("VtParser places the cursor from one-based coordinates on the wire onto zero-based cells")
{
	Core::TerminalScreen screen(8, 4);
	Core::VtParser parser(screen);

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

	CHECK(ScreenRowText(screen, 0) == U"ab    ");
}

TEST_CASE("VtParser erases the display")
{
	Core::TerminalScreen screen(4, 2);
	Core::VtParser parser(screen);

	parser.Consume("ab\x1B[2J");

	CHECK(ScreenRowText(screen, 0) == U"    ");
}

TEST_CASE("VtParser sets a scroll region and reverse-indexes inside it")
{
	Core::TerminalScreen screen(4, 4);
	Core::VtParser parser(screen);

	parser.Consume("\x1B[2;3r");
	parser.Consume("\x1B[2;1Hone\x1BM");

	CHECK(ScreenRowText(screen, 1) == U"    ");
	CHECK(ScreenRowText(screen, 2) == U"one ");
}

TEST_CASE("VtParser handles a sequence split across two reads")
{
	Core::TerminalScreen screen(8, 4);
	Core::VtParser parser(screen);

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

	CHECK(ScreenRowText(screen, 0) == U"ab      ");
}

TEST_CASE("VtParser swallows an operating system command and takes the title from it")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	parser.Consume("\x1B]0;git status\x07ok");

	CHECK(screen.GetTitle() == "git status");
	CHECK(ScreenRowText(screen, 0) == U"ok      ");
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

TEST_CASE("VtParser lets an ESC abort the sequence in progress and open the next one")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	parser.Consume("\x1B[3\x1B[2Jab");

	CHECK(ScreenRowText(screen, 0) == U"ab      ");
}

TEST_CASE("VtParser starts a fresh sequence on a doubled ESC")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	parser.Consume("\x1B\x1B[1;3Hx");

	CHECK(ScreenRowText(screen, 0) == U"  x     ");
}

TEST_CASE("VtParser neither prints nor dispatches a CSI opened with a prefix other than '?' such as modifyOtherKeys")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	const std::string reply = parser.Consume("a\x1B[>4;2mb");

	CHECK(ScreenRowText(screen, 0) == U"ab      ");
	CHECK(reply.empty());
	CHECK(screen.At(0, 1).Flags == Core::CellFlags::None);
}

TEST_CASE("VtParser answers the secondary device attributes request that tmux waits for")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	CHECK(parser.Consume("\x1B[>0c") == "\x1B[>0;10;1c");
}

TEST_CASE("VtParser keeps a leading empty parameter so ESC[;5H means the default row and column five")
{
	Core::TerminalScreen screen(8, 4);
	Core::VtParser parser(screen);

	parser.Consume("\x1B[;5Hx");

	CHECK(screen.At(0, 4).Code == U'x');
}

TEST_CASE("VtParser ignores scroll left rather than insert characters that share its final byte")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	parser.Consume("abc\x1B[1;1H\x1B[1 @");

	CHECK(ScreenRowText(screen, 0) == U"abc     ");
}

TEST_CASE("VtParser drops a title whose sequence an ESC abandons")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	parser.Consume("\x1B]0;first\x07");
	parser.Consume("\x1B]0;second\x1B[2Jok");

	CHECK(screen.GetTitle() == "first");
	CHECK(ScreenRowText(screen, 0) == U"ok      ");
}

TEST_CASE("VtParser takes a title from a string-terminated operating system command")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	parser.Consume("\x1B]2;built\x1B\\ok");

	CHECK(screen.GetTitle() == "built");
	CHECK(ScreenRowText(screen, 0) == U"ok      ");
}

TEST_CASE("VtParser answers a cursor position report in the one-based coordinates a CUP takes")
{
	Core::TerminalScreen screen(8, 4);
	Core::VtParser parser(screen);

	parser.Consume("\x1B[3;5H");

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

	const std::size_t floodedSeparatorCount = 4096;
	const std::size_t floodedTitleLength = 65536;

	parser.Consume("\x1B[" + std::string(floodedSeparatorCount, ';') + "H");
	parser.Consume("\x1B]0;" + std::string(floodedTitleLength, 'x') + "\x07" + "ok");

	CHECK(screen.GetTitle().size() < floodedTitleLength);
	CHECK(ScreenRowText(screen, 0) == U"ok      ");
}

TEST_CASE("VtParser drops a bold code that falls past the parameter cap")
{
	Core::TerminalScreen screen(4, 2);
	Core::VtParser parser(screen);

	const int resetCodesBeforeBold = 49;
	std::string sequence = "\x1B[";
	for (int repetition = 0; repetition < resetCodesBeforeBold; ++repetition)
	{
		sequence += "0;";
	}
	sequence += "1ma";

	parser.Consume(sequence);

	CHECK_FALSE(Core::HasFlag(screen.At(0, 0).Flags, Core::CellFlags::Bold));
}

TEST_CASE("VtParser swallows a CSI flooded with intermediate bytes and prints what follows")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	const std::size_t floodedIntermediateCount = 4096;

	parser.Consume("\x1B[" + std::string(floodedIntermediateCount, ' ') + "@");
	parser.Consume("ok");

	CHECK(ScreenRowText(screen, 0) == U"ok      ");
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

	const std::string_view allThreeMouseModesThenSgrEncoding = "\x1B[?1000h\x1B[?1002h\x1B[?1003h\x1B[?1006h";
	parser.Consume(allThreeMouseModesThenSgrEncoding);
	CHECK(screen.IsMouseReporting());
	CHECK(screen.IsSgrMouse());

	const std::string_view motionOffWithClicksAndDragsStillOn = "\x1B[?1003l";
	parser.Consume(motionOffWithClicksAndDragsStillOn);
	CHECK(screen.IsMouseReporting());

	parser.Consume("\x1B[?1000l\x1B[?1002l\x1B[?1006l");
	CHECK_FALSE(screen.IsMouseReporting());
	CHECK_FALSE(screen.IsSgrMouse());
}

TEST_CASE("VtParser ignores a public mode rather than mistake it for the DEC private mode with the same number")
{
	Core::TerminalScreen screen(4, 2);
	Core::VtParser parser(screen);

	parser.Consume("\x1B[4h\x1B[7l\x1B[25l\x1B[1049h");

	CHECK(screen.IsAutoWrap());
	CHECK(screen.IsCursorVisible());
	CHECK_FALSE(screen.IsAlternateBuffer());
}

TEST_CASE("VtParser decodes a multi-byte character")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	parser.Consume("\xC5\x82");

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

	parser.Consume("\xE4\xBD\xA0");

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

TEST_CASE("VtParser re-reads the non-continuation byte that broke a multi-byte character as a fresh character")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

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

	parser.Consume("\xC5");
	parser.Consume("Z");

	CHECK(screen.At(0, 0).Code == U'\xFFFD');
	CHECK(screen.At(0, 1).Code == U'Z');
	CHECK(screen.GetCursor().Column == 2);
}

TEST_CASE("VtParser replaces both bytes of an overlong slash rather than decode the slash it disguises")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	parser.Consume("\xC0\xAF"
				   "b");

	CHECK(screen.At(0, 0).Code == Core::kReplacementCharacter);
	CHECK(screen.At(0, 1).Code == Core::kReplacementCharacter);
	CHECK(screen.At(0, 2).Code == U'b');
}

TEST_CASE("VtParser does not decode an encoded surrogate and replaces each of its three bytes")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

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

TEST_CASE("VtParser replaces a continuation byte that arrives with nothing pending rather than fold it into the previous character")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	parser.Consume("a"
				   "\x82"
				   "b");

	CHECK(screen.At(0, 0).Code == U'a');
	CHECK(screen.At(0, 1).Code == U'\xFFFD');
	CHECK(screen.At(0, 2).Code == U'b');
}

TEST_CASE("VtParser swallows whole the character-set selection that ncurses sends on every attribute reset")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	parser.Consume("\x1B(B"
				   "hi");

	CHECK(ScreenRowText(screen, 0) == U"hi      ");
	CHECK(screen.GetCursor().Column == 2);
}

TEST_CASE("VtParser swallows the line-drawing character set and a DEC alignment test")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	parser.Consume("\x1B(0"
				   "a");
	parser.Consume("\x1B#8"
				   "b");

	CHECK(ScreenRowText(screen, 0) == U"ab      ");
	CHECK(screen.GetCursor().Column == 2);
}

TEST_CASE("VtParser clamps the largest repetition count to the rows and columns that exist")
{
	Core::TerminalScreen screen(8, 5);
	Core::VtParser parser(screen);

	parser.Consume("hello");

	parser.Consume("\x1B[65535S");

	CHECK(screen.GetScrollbackCount() == 5);
	CHECK(ScreenRowText(screen, 0) == U"        ");
	CHECK(screen.GetRows() == 5);
	CHECK(screen.GetColumns() == 8);

	parser.Consume("\x1B[65535L\x1B[65535M\x1B[65535@\x1B[65535P\x1B[65535T");

	CHECK(screen.GetRows() == 5);
	CHECK(screen.GetColumns() == 8);
	CHECK(ScreenRowText(screen, 0) == U"        ");
}

TEST_CASE("VtParser drops a CSI with a colon sub-parameter whole")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	parser.Consume("\x1B[4:3mX");

	CHECK(ScreenRowText(screen, 0) == U"X       ");
}

TEST_CASE("VtParser drops a colon-separated true-colour sequence whole")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	parser.Consume("\x1B[38:2::1:2:3mX");

	CHECK(ScreenRowText(screen, 0) == U"X       ");
}

TEST_CASE("VtParser drops a CSI with a private byte after the first parameter byte")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	parser.Consume("\x1B[1?2mX");

	CHECK(ScreenRowText(screen, 0) == U"X       ");
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

	CHECK(ScreenRowText(screen, 0) == U"ab      ");
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

	const std::string_view lineFeedOnTheLastRowOfAFullHeightRegion = "\x1B[6;1H\n";
	parser.Consume(lineFeedOnTheLastRowOfAFullHeightRegion);
	CHECK(screen.GetScrollbackCount() == 1);
	CHECK(ScreenRowText(screen, 0) == U"        ");
}

TEST_CASE("VtParser abandons an ignored CSI on CAN as the interpreting states do on any control byte")
{
	Core::TerminalScreen screen(8, 2);
	Core::VtParser parser(screen);

	parser.Consume("\x1B[4:\x18"
				   "abc");

	CHECK(ScreenRowText(screen, 0) == U"abc     ");
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
