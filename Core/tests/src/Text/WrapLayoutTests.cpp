#include <doctest/doctest.h>

#include "Core/Text/WrapLayout.h"
#include "Core/Text/Columns.h"
#include "Core/Text/Encoding.h"
#include "Core/Text/RowMap.h"
#include "Core/Text/TextBuffer.h"

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

using namespace Core;

namespace {
	constexpr std::size_t kTabWidth = 4;
	constexpr std::size_t kWideEnoughNotToWrap = 80;
	constexpr std::size_t kFarPastAnyRowEnd = 99;
	constexpr std::size_t kFirstLine = 0;
}

TEST_CASE("A line that fits does not break")
{
	const std::size_t columnsPerRow = 10;

	CHECK(WrapColumns("", columnsPerRow, kTabWidth).empty());
	CHECK(WrapColumns("short", columnsPerRow, kTabWidth).empty());
}

TEST_CASE("A line exactly as wide as the row does not break because a break after its last cell would leave an empty row")
{
	const std::size_t columnsPerRow = 10;

	CHECK(WrapColumns("0123456789", columnsPerRow, kTabWidth).empty());
}

TEST_CASE("A break goes after the last space that fits")
{
	const std::size_t columnsPerRow = 12;
	const std::size_t firstColumnOfGamma = 11;

	const std::vector<std::size_t> breaks = WrapColumns("alpha beta gamma", columnsPerRow, kTabWidth);

	REQUIRE(breaks.size() == 1);
	CHECK(breaks[0] == firstColumnOfGamma);
}

TEST_CASE("A word longer than the row breaks hard at the limit")
{
	const std::size_t columnsPerRow = 5;

	const std::vector<std::size_t> breaks = WrapColumns("aaaaaaaaaaaaaaa", columnsPerRow, kTabWidth);

	REQUIRE(breaks.size() == 2);
	CHECK(breaks[0] == 5);
	CHECK(breaks[1] == 10);
}

TEST_CASE("Runs of spaces stay on the row they end so the next row starts at a letter")
{
	const std::size_t columnsPerRow = 6;
	const std::size_t firstColumnOfSecondWord = 6;

	const std::vector<std::size_t> breaks = WrapColumns("aa    bb", columnsPerRow, kTabWidth);

	REQUIRE(breaks.size() == 1);
	CHECK(breaks[0] == firstColumnOfSecondWord);
}

TEST_CASE("A tab counts as the columns it advances, not as one character")
{
	const std::string_view lineText = "\tabcdef";
	const std::size_t columnsPerRow = 6;
	const std::size_t fourTabColumnsAndSixLetters = 10;
	const std::size_t columnOfThirdLetter = 6;

	CHECK(ColumnCount(lineText, kTabWidth) == fourTabColumnsAndSixLetters);

	const std::vector<std::size_t> breaks = WrapColumns(lineText, columnsPerRow, kTabWidth);

	REQUIRE(breaks.size() == 1);
	CHECK(breaks[0] == columnOfThirdLetter);
}

TEST_CASE("A character outside ASCII is one column whatever its byte length")
{
	const std::size_t columnsPerRow = 2;
	const std::size_t columnAfterTwoCharacters = 2;

	const std::vector<std::size_t> breaks = WrapColumns("ąąąą", columnsPerRow, kTabWidth);

	REQUIRE(breaks.size() == 1);
	CHECK(breaks[0] == columnAfterTwoCharacters);
}

TEST_CASE("A space after letters outside ASCII breaks at its own column because the scan steps over whole characters and never single bytes")
{
	const std::size_t columnsPerRow = 5;
	const std::size_t firstColumnOfSecondWord = 3;

	const std::vector<std::size_t> breaks = WrapColumns("ąą bb cc", columnsPerRow, kTabWidth);

	REQUIRE(breaks.size() == 1);
	CHECK(breaks[0] == firstColumnOfSecondWord);
}

TEST_CASE("A row width below one cell from a window dragged that narrow is a layout state that still terminates")
{
	const std::size_t oneCellWide = 1;
	const std::size_t noCellsWide = 0;

	CHECK(WrapColumns("abc", oneCellWide, kTabWidth).size() == 2);
	CHECK(WrapColumns("abc", noCellsWide, kTabWidth).size() == 2);
}

TEST_CASE("A space run reaching past the limit stays whole on its row because trailing spaces are invisible and leading ones are not")
{
	const std::size_t columnsPerRow = 4;
	const std::size_t firstColumnAfterTheRun = 8;

	const std::vector<std::size_t> breaks = WrapColumns("aa      bb", columnsPerRow, kTabWidth);

	REQUIRE(breaks.size() == 1);
	CHECK(breaks[0] == firstColumnAfterTheRun);
}

TEST_CASE("A line of nothing but spaces never breaks")
{
	const std::size_t columnsPerRow = 3;

	CHECK(WrapColumns("      ", columnsPerRow, kTabWidth).empty());
}

TEST_CASE("A hard break never lands inside a tab so the whole tab moves to the next row")
{
	const std::size_t columnsPerRow = 6;
	const std::size_t columnWhereTabStarts = 4;

	const std::vector<std::size_t> breaks = WrapColumns("aaaa\tbbbb", columnsPerRow, kTabWidth);

	REQUIRE(!breaks.empty());
	CHECK(breaks[0] == columnWhereTabStarts);
}

TEST_CASE("A tab wider than the row at the row's own start cannot move back so the break goes after it")
{
	const std::size_t columnsPerRow = 2;
	const std::size_t columnAfterTab = 4;

	const std::vector<std::size_t> breaks = WrapColumns("\tabc", columnsPerRow, kTabWidth);

	REQUIRE(!breaks.empty());
	CHECK(breaks[0] == columnAfterTab);
}

TEST_CASE("A tab wider than the row at the end of a line does not add an empty row")
{
	const std::string_view lineText = "bbbb\t";
	const std::size_t columnsPerRow = 3;
	const std::size_t columnAfterTab = 8;

	CHECK(ColumnCount(lineText, kTabWidth) == columnAfterTab);

	const std::vector<std::size_t> breaks = WrapColumns(lineText, columnsPerRow, kTabWidth);

	REQUIRE(!breaks.empty());
	CHECK(breaks.back() < ColumnCount(lineText, kTabWidth));
}

TEST_CASE("A line of a single tab never breaks")
{
	const std::size_t oneCellWide = 1;

	CHECK(WrapColumns("\t", oneCellWide, kTabWidth).empty());
}

TEST_CASE("An offset on an unwrapped line maps to its own row and column")
{
	const TextBuffer buffer{std::string{"abc\ndef"}};
	const std::size_t offsetOfE = 5;

	RowMap rows;
	rows.Rebuild(buffer, kWideEnoughNotToWrap, kTabWidth);

	const VisualPosition position = CaretToVisual(buffer, rows, offsetOfE, kTabWidth);

	CHECK(position.Row == 1);
	CHECK(position.Column == 1);
}

TEST_CASE("An offset on a break is the first cell of the following row and never the last of the row before")
{
	const TextBuffer buffer{std::string{"aaaaaaaaaa"}};
	const std::size_t columnsPerRow = 4;
	const std::vector<std::size_t> rowsAfterTheFirstStartAt{4, 8};
	const std::size_t offsetOnFirstBreak = 4;

	RowMap rows;
	rows.Rebuild(buffer, columnsPerRow, kTabWidth);

	CHECK(rows.BreaksOf(kFirstLine) == rowsAfterTheFirstStartAt);

	const VisualPosition position = CaretToVisual(buffer, rows, offsetOnFirstBreak, kTabWidth);

	CHECK(position.Row == 1);
	CHECK(position.Column == 0);
}

TEST_CASE("A column past the end of a row clamps to that row's last position and never to the next row's first cell")
{
	const TextBuffer buffer{std::string{"abc\ndef"}};
	const VisualPosition farRightOfFirstRow{.Row = 0, .Column = kFarPastAnyRowEnd};
	const std::size_t endOfShortFirstLine = 3;

	RowMap unwrapped;
	unwrapped.Rebuild(buffer, kWideEnoughNotToWrap, kTabWidth);

	CHECK(VisualToCaret(buffer, unwrapped, farRightOfFirstRow, kTabWidth) == endOfShortFirstLine);

	const TextBuffer wrapped{std::string{"aaaaaaaaaa"}};
	const std::size_t fourColumnsPerRow = 4;
	const std::size_t lastCharacterOfFirstRow = 3;

	RowMap wrappedRows;
	wrappedRows.Rebuild(wrapped, fourColumnsPerRow, kTabWidth);

	CHECK(VisualToCaret(wrapped, wrappedRows, farRightOfFirstRow, kTabWidth) == lastCharacterOfFirstRow);

	const TextBuffer spaceWrapped{std::string{"alpha beta gamma"}};
	const std::size_t sixColumnsPerRow = 6;
	const std::vector<std::size_t> breaksAfterAlphaAndBeta{6, 11};
	const std::size_t caretBeforeSpaceAfterAlpha = 5;
	const VisualPosition farRightOfMiddleRow{.Row = 1, .Column = kFarPastAnyRowEnd};
	const std::size_t caretBeforeSpaceAfterBeta = 10;

	RowMap spaceWrappedRows;
	spaceWrappedRows.Rebuild(spaceWrapped, sixColumnsPerRow, kTabWidth);

	CHECK(spaceWrappedRows.BreaksOf(kFirstLine) == breaksAfterAlphaAndBeta);
	CHECK(VisualToCaret(spaceWrapped, spaceWrappedRows, farRightOfFirstRow, kTabWidth) == caretBeforeSpaceAfterAlpha);
	CHECK(VisualToCaret(spaceWrapped, spaceWrappedRows, farRightOfMiddleRow, kTabWidth) == caretBeforeSpaceAfterBeta);
}

TEST_CASE("Both directions compose to the identity at every byte offset of ASCII text because drawing and hit-testing and caret movement all go through them")
{
	const TextBuffer buffer{std::string{"alpha beta gamma\n\tindented line\nx\n\nlast"}};
	const std::size_t columnsPerRow = 6;

	RowMap rows;
	rows.Rebuild(buffer, columnsPerRow, kTabWidth);

	for (std::size_t offset = 0; offset <= buffer.GetLength(); ++offset)
	{
		const VisualPosition position = CaretToVisual(buffer, rows, offset, kTabWidth);

		CAPTURE(offset);
		CHECK(VisualToCaret(buffer, rows, position, kTabWidth) == offset);
	}
}

TEST_CASE("Both directions compose to the identity at every character boundary of text outside ASCII since no caret sits inside a character")
{
	const TextBuffer buffer{std::string{"zazżółć gęślą jaźń\n\tąćę wrapped\n€€€"}};
	const std::size_t columnsPerRow = 6;

	RowMap rows;
	rows.Rebuild(buffer, columnsPerRow, kTabWidth);

	const std::string text = buffer.Materialize();
	std::size_t offset = 0;

	while (true)
	{
		const VisualPosition position = CaretToVisual(buffer, rows, offset, kTabWidth);

		CAPTURE(offset);
		CHECK(VisualToCaret(buffer, rows, position, kTabWidth) == offset);

		if (offset >= text.size())
		{
			break;
		}

		offset = NextCharacter(text, offset);
	}
}

TEST_CASE("Wrapping a 200 000 character line stays linear because the UI thread wraps it with no way to cancel the frame")
{
	const std::size_t blockCount = 500;
	const std::size_t blockColumns = 400;
	const std::size_t lettersBeforeEachSpace = blockColumns - 1;
	const std::size_t lineLength = blockCount * blockColumns;
	const std::size_t columnsPerRow = 120;
	const std::size_t hardBreaksPerBlock = lettersBeforeEachSpace / columnsPerRow;
	const std::size_t breaksPerFullBlock = hardBreaksPerBlock + 1;
	const std::size_t fullBlocksBeforeTheLast = blockCount - 1;
	const std::size_t hardBreaksInLastBlock = hardBreaksPerBlock;
	const std::size_t indexOfFirstBreakPastASpace = hardBreaksPerBlock;

	std::string lineText;
	lineText.reserve(lineLength);

	for (std::size_t blockIndex = 0; blockIndex < blockCount; ++blockIndex)
	{
		lineText.append(lettersBeforeEachSpace, 'a');
		lineText.push_back(' ');
	}

	REQUIRE(lineText.size() == lineLength);

	const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
	const std::vector<std::size_t> breaks = WrapColumns(lineText, columnsPerRow, kTabWidth);
	const std::chrono::milliseconds::rep elapsedMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();

	CHECK(breaks.size() == fullBlocksBeforeTheLast * breaksPerFullBlock + hardBreaksInLastBlock);
	CHECK(breaks.front() == columnsPerRow);
	CHECK(breaks[indexOfFirstBreakPastASpace] == blockColumns);

	// Loose on purpose so it is never flaky on a cold, unoptimised, instrumented Debug build; a walk
	// quadratic in the line's length would still overrun it several times over.
	const std::chrono::milliseconds::rep budgetMilliseconds = 2000;
	CHECK(elapsedMilliseconds < budgetMilliseconds);
}
