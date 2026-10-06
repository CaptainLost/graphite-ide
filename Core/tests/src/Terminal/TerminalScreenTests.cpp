#include <doctest/doctest.h>

#include "Core/Terminal/CharacterWidth.h"
#include "Core/Terminal/TerminalScreen.h"

#include "TerminalFixtures.h"

#include <string_view>

namespace {
	constexpr bool kSwitchToAlternate = true;
	constexpr bool kSwitchBackToPrimary = false;

	void PrintAscii(Core::TerminalScreen& screen, std::string_view text)
	{
		for (const char character : text)
		{
			screen.Print(static_cast<char32_t>(character), Core::kNarrowCharacterCells);
		}
	}
}

TEST_CASE("TerminalScreen starts blank with the cursor at the origin")
{
	Core::TerminalScreen screen(4, 2);

	CHECK(ScreenRowText(screen, 0) == U"    ");
	CHECK(screen.GetCursor().Row == 0);
	CHECK(screen.GetCursor().Column == 0);
}

TEST_CASE("TerminalScreen prints characters and advances the cursor")
{
	Core::TerminalScreen screen(4, 2);

	PrintAscii(screen, "ab");

	CHECK(ScreenRowText(screen, 0) == U"ab  ");
	CHECK(screen.GetCursor().Column == 2);
}

TEST_CASE("TerminalScreen keeps the cursor on the last column and defers the wrap until the character after it")
{
	Core::TerminalScreen screen(2, 2);

	PrintAscii(screen, "ab");

	CHECK(screen.GetCursor().Row == 0);
	CHECK(screen.GetCursor().Column == 1);

	PrintAscii(screen, "c");

	CHECK(screen.GetCursor().Row == 1);
	CHECK(screen.GetCursor().Column == 1);
	CHECK(ScreenRowText(screen, 0) == U"ab");
	CHECK(ScreenRowText(screen, 1) == U"c ");
}

TEST_CASE("TerminalScreen cancels a pending wrap on a carriage return")
{
	Core::TerminalScreen screen(2, 2);

	PrintAscii(screen, "ab");
	screen.CarriageReturn();
	PrintAscii(screen, "c");

	CHECK(ScreenRowText(screen, 0) == U"cb");
	CHECK(ScreenRowText(screen, 1) == U"  ");
}

TEST_CASE("TerminalScreen overwrites a line in place after a carriage return")
{
	Core::TerminalScreen screen(8, 2);

	PrintAscii(screen, "50%");
	screen.CarriageReturn();
	PrintAscii(screen, "75%");

	CHECK(ScreenRowText(screen, 0) == U"75%     ");
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

	screen.Print(U'你', Core::kWideCharacterCells);

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

TEST_CASE("TerminalScreen wraps a double-width character whole when one column is left instead of splitting it at the margin")
{
	Core::TerminalScreen screen(3, 2);

	PrintAscii(screen, "ab");
	screen.Print(U'你', Core::kWideCharacterCells);

	CHECK(ScreenRowText(screen, 0) == U"ab ");
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

	CHECK(ScreenRowText(screen, 0) == U"ab    ");
}

TEST_CASE("TerminalScreen erases from the start of the line through the cell under the cursor")
{
	Core::TerminalScreen screen(6, 2);

	PrintAscii(screen, "abcdef");
	screen.SetCursor(0, 2);
	screen.EraseInLine(Core::EraseExtent::ToStart);

	CHECK(ScreenRowText(screen, 0) == U"   def");
}

TEST_CASE("TerminalScreen erases the whole display")
{
	Core::TerminalScreen screen(4, 2);

	PrintAscii(screen, "ab");
	screen.LineFeed();
	screen.CarriageReturn();
	PrintAscii(screen, "cd");
	screen.EraseInDisplay(Core::EraseExtent::All);

	CHECK(ScreenRowText(screen, 0) == U"    ");
	CHECK(ScreenRowText(screen, 1) == U"    ");
}

TEST_CASE("TerminalScreen erases from the cell under the cursor to the end of the display and leaves the rows above alone")
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

	CHECK(ScreenRowText(screen, 0) == U"abcd");
	CHECK(ScreenRowText(screen, 1) == U"ef  ");
	CHECK(ScreenRowText(screen, 2) == U"    ");
}

TEST_CASE("TerminalScreen erases from the start of the display through the cell under the cursor and leaves the rows below alone")
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

	CHECK(ScreenRowText(screen, 0) == U"    ");
	CHECK(ScreenRowText(screen, 1) == U"   h");
	CHECK(ScreenRowText(screen, 2) == U"ijkl");
}

TEST_CASE("TerminalScreen deletes characters and pulls the rest of the line left")
{
	Core::TerminalScreen screen(6, 1);

	PrintAscii(screen, "abcdef");
	screen.SetCursor(0, 1);
	screen.DeleteCharacters(2);

	CHECK(ScreenRowText(screen, 0) == U"adef  ");
}

TEST_CASE("TerminalScreen inserts characters and pushes the rest of the line right")
{
	Core::TerminalScreen screen(6, 1);

	PrintAscii(screen, "abcdef");
	screen.SetCursor(0, 1);
	screen.InsertCharacters(2);

	CHECK(ScreenRowText(screen, 0) == U"a  bcd");
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
	CHECK(ScreenRowText(screen, 0) == U"two  ");
}

TEST_CASE("TerminalScreen keeps the scrollback within its limit")
{
	constexpr std::size_t kScrollbackLimit = 2;
	constexpr int kFeedsPastTheLimit = 6;
	Core::TerminalScreen screen(4, 1, kScrollbackLimit);

	for (int feed = 0; feed < kFeedsPastTheLimit; ++feed)
	{
		screen.LineFeed();
	}

	CHECK(screen.GetScrollbackCount() == kScrollbackLimit);
}

TEST_CASE("TerminalScreen scrolls only inside the scroll region and does not keep the line it pushes out as history")
{
	Core::TerminalScreen screen(4, 4);

	screen.SetCursor(0, 0);
	PrintAscii(screen, "top");
	screen.SetScrollRegion(1, 2);
	screen.SetCursor(2, 0);
	PrintAscii(screen, "mid");
	screen.LineFeed();

	CHECK(ScreenRowText(screen, 0) == U"top ");
	CHECK(ScreenRowText(screen, 1) == U"mid ");
	CHECK(screen.GetScrollbackCount() == 0);
}

TEST_CASE("TerminalScreen reverse index scrolls down at the top of the region")
{
	Core::TerminalScreen screen(4, 3);

	screen.SetCursor(0, 0);
	PrintAscii(screen, "one");
	screen.SetCursor(0, 0);
	screen.ReverseIndex();

	CHECK(ScreenRowText(screen, 0) == U"    ");
	CHECK(ScreenRowText(screen, 1) == U"one ");
}

TEST_CASE("TerminalScreen leaving the alternate buffer restores the primary untouched")
{
	Core::TerminalScreen screen(4, 2);

	constexpr std::uint16_t kPrimaryCursorRow = 1;
	constexpr std::uint16_t kPrimaryCursorColumn = 2;

	PrintAscii(screen, "keep");
	screen.SetCursor(kPrimaryCursorRow, kPrimaryCursorColumn);

	screen.UseAlternateBuffer(kSwitchToAlternate);
	CHECK(ScreenRowText(screen, 0) == U"    ");
	screen.SetCursor(0, 0);
	PrintAscii(screen, "editor stuff");

	const Core::CursorPosition alternateCursor = screen.GetCursor();
	const bool alternateCursorIsElsewhere = alternateCursor.Row != kPrimaryCursorRow || alternateCursor.Column != kPrimaryCursorColumn;
	CHECK(alternateCursorIsElsewhere);

	screen.UseAlternateBuffer(kSwitchBackToPrimary);

	CHECK(ScreenRowText(screen, 0) == U"keep");
	CHECK(screen.GetCursor().Row == kPrimaryCursorRow);
	CHECK(screen.GetCursor().Column == kPrimaryCursorColumn);
}

TEST_CASE("TerminalScreen alternate buffer does not feed the scrollback")
{
	Core::TerminalScreen screen(4, 1);

	screen.UseAlternateBuffer(kSwitchToAlternate);

	for (int feed = 0; feed < 4; ++feed)
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
	CHECK(ScreenRowText(screen, 0) == U"abc");

	screen.Resize(5, 2);

	CHECK(ScreenRowText(screen, 0) == U"abc  ");
}

TEST_CASE("TerminalScreen clamps a cursor restored from the alternate buffer onto a screen shrunk past its row so line feeds still advance")
{
	Core::TerminalScreen screen(4, 10);

	constexpr std::uint16_t kRowBeyondShrunkScreen = 8;

	screen.SetCursor(kRowBeyondShrunkScreen, 0);
	screen.UseAlternateBuffer(kSwitchToAlternate);

	screen.Resize(4, 3);
	CHECK(kRowBeyondShrunkScreen >= screen.GetRows());

	screen.UseAlternateBuffer(kSwitchBackToPrimary);

	CHECK(screen.GetCursor().Row < screen.GetRows());

	PrintAscii(screen, "a");
	screen.LineFeed();
	screen.CarriageReturn();
	PrintAscii(screen, "b");

	CHECK(ScreenRowText(screen, 1) == U"a   ");
	CHECK(ScreenRowText(screen, 2) == U"b   ");
}

TEST_CASE("TerminalScreen counts every row scrolled into the scrollback, past its limit too")
{
	constexpr std::size_t kScrollbackLimit = 3;
	constexpr std::uint16_t kRowCount = 2;
	Core::TerminalScreen screen(4, kRowCount, kScrollbackLimit);

	CHECK(screen.GetRowsScrolledOff() == 0);

	constexpr int kFeedCount = 3 * static_cast<int>(kScrollbackLimit);

	for (int feed = 0; feed < kFeedCount; ++feed)
	{
		screen.LineFeed();
	}

	constexpr int kFeedsWalkingDownToBottomRow = kRowCount - 1;
	const auto rowsPushedOffTop = static_cast<std::uint64_t>(kFeedCount - kFeedsWalkingDownToBottomRow);

	CHECK(screen.GetScrollbackCount() == kScrollbackLimit);
	CHECK(screen.GetRowsScrolledOff() == rowsPushedOffTop);

	screen.ClearScrollback();

	CHECK(screen.GetScrollbackCount() == 0);
	CHECK(screen.GetRowsScrolledOff() == rowsPushedOffTop);

	screen.LineFeed();

	CHECK(screen.GetRowsScrolledOff() == rowsPushedOffTop + 1);
}

TEST_CASE("TerminalScreen does not count rows that never reach the scrollback")
{
	Core::TerminalScreen screen(4, 4);

	screen.SetScrollRegion(1, 2);
	screen.ScrollUp(2);
	screen.ResetScrollRegion();

	screen.UseAlternateBuffer(kSwitchToAlternate);
	screen.ScrollUp(3);
	screen.UseAlternateBuffer(kSwitchBackToPrimary);

	CHECK(screen.GetRowsScrolledOff() == 0);
	CHECK(screen.GetScrollbackCount() == 0);
}
