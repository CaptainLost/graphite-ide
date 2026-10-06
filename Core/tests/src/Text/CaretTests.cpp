#include <doctest/doctest.h>

#include "Core/Text/Caret.h"
#include "Core/Text/Encoding.h"
#include "Core/Text/TextBuffer.h"

#include <limits>
#include <ostream>
#include <string>

using namespace Core;

namespace {
	constexpr std::size_t kTabWidth = 4;
	constexpr std::size_t kPageLines = 10;

	const std::string kLetterAWithOgonek = "\xC4\x85";
	const std::string kLetterEWithOgonek = "\xC4\x99";
	const std::string kSlightlySmilingFace = "\xF0\x9F\x99\x82";

	Caret CaretWithoutDesiredColumnAt(std::size_t offset)
	{
		return Caret{.Offset = offset, .Anchor = offset, .DesiredColumn = NoDesiredColumn};
	}

	Caret Step(const TextBuffer& buffer, Caret caret, Motion motion)
	{
		return Move(buffer, caret, motion, SelectionChange::Collapse, kTabWidth, kPageLines);
	}

	Caret StepExtendingSelection(const TextBuffer& buffer, Caret caret, Motion motion)
	{
		return Move(buffer, caret, motion, SelectionChange::Extend, kTabWidth, kPageLines);
	}
}

TEST_CASE("A fresh caret has no selection")
{
	const Caret caret;

	CHECK_FALSE(HasSelection(caret));
	CHECK(SelectionRange(caret) == OffsetRange{.From = 0, .To = 0});
}

TEST_CASE("A selection reads the same whichever way it was dragged")
{
	const Caret forward{.Offset = 7, .Anchor = 2, .DesiredColumn = 0};
	const Caret backward{.Offset = 2, .Anchor = 7, .DesiredColumn = 0};

	CHECK(HasSelection(forward));
	CHECK(HasSelection(backward));
	CHECK(SelectionRange(forward) == SelectionRange(backward));
	CHECK(SelectionRange(forward) == OffsetRange{.From = 2, .To = 7});
}

TEST_CASE("Left and right move by a character, not a byte")
{
	const TextBuffer buffer{"a" + kLetterAWithOgonek + kSlightlySmilingFace + "z"};

	Caret caret = CaretWithoutDesiredColumnAt(0);
	caret = Step(buffer, caret, Motion::Right);
	CHECK(caret.Offset == 1);
	caret = Step(buffer, caret, Motion::Right);
	CHECK(caret.Offset == 3);
	caret = Step(buffer, caret, Motion::Right);
	CHECK(caret.Offset == 7);

	caret = Step(buffer, caret, Motion::Left);
	CHECK(caret.Offset == 3);
	caret = Step(buffer, caret, Motion::Left);
	CHECK(caret.Offset == 1);
}

TEST_CASE("Movement stops at the ends of the document")
{
	const TextBuffer buffer{"ab"};

	CHECK(Step(buffer, CaretWithoutDesiredColumnAt(0), Motion::Left).Offset == 0);
	CHECK(Step(buffer, CaretWithoutDesiredColumnAt(2), Motion::Right).Offset == 2);
}

TEST_CASE("Left and right cross line boundaries")
{
	const TextBuffer buffer{"ab\ncd"};

	CHECK(Step(buffer, CaretWithoutDesiredColumnAt(2), Motion::Right).Offset == 3);
	CHECK(Step(buffer, CaretWithoutDesiredColumnAt(3), Motion::Left).Offset == 2);
}

TEST_CASE("Home and End go to the ends of the line, not the document")
{
	const TextBuffer buffer{"first\nsecond\nthird"};

	CHECK(Step(buffer, CaretWithoutDesiredColumnAt(9), Motion::LineStart).Offset == 6);
	CHECK(Step(buffer, CaretWithoutDesiredColumnAt(9), Motion::LineEnd).Offset == 12);
}

TEST_CASE("Ctrl+Home and Ctrl+End go to the ends of the document")
{
	const TextBuffer buffer{"first\nsecond"};

	CHECK(Step(buffer, CaretWithoutDesiredColumnAt(8), Motion::DocumentStart).Offset == 0);
	CHECK(Step(buffer, CaretWithoutDesiredColumnAt(2), Motion::DocumentEnd).Offset == buffer.GetLength());
}

TEST_CASE("Down keeps the column across a shorter line and comes back to it on the next long line")
{
	const TextBuffer buffer{"aaaaaaaa\nbb\ncccccccc"};
	const std::size_t columnFiveOfFirstLine = 5;
	const std::size_t endOfShortLine = 11;
	const std::size_t startOfLastLine = 12;

	Caret caret = CaretWithoutDesiredColumnAt(columnFiveOfFirstLine);
	caret = Step(buffer, caret, Motion::Down);
	CHECK(caret.Offset == endOfShortLine);

	caret = Step(buffer, caret, Motion::Down);
	CHECK(caret.Offset == startOfLastLine + columnFiveOfFirstLine);
}

TEST_CASE("A stored column smaller than the actual one still wins")
{
	const TextBuffer buffer{"aaaaaaaa\nbb\ncccccccc"};
	const std::size_t storedColumn = 1;
	const std::size_t startOfShortLine = 9;
	const std::size_t columnFiveOfFirstLine = 5;

	const Caret caret{.Offset = columnFiveOfFirstLine, .Anchor = columnFiveOfFirstLine, .DesiredColumn = storedColumn};
	CHECK(Step(buffer, caret, Motion::Down).Offset == startOfShortLine + storedColumn);
}

TEST_CASE("A horizontal move resets the column the caret aims for")
{
	const TextBuffer buffer{"aaaaaaaa\nbb\ncccccccc"};
	const std::size_t startOfLastLine = 12;
	const std::size_t columnAfterMovingLeft = 1;

	Caret caret = CaretWithoutDesiredColumnAt(5);
	caret = Step(buffer, caret, Motion::Down);
	caret = Step(buffer, caret, Motion::Left);
	caret = Step(buffer, caret, Motion::Down);

	CHECK(caret.Offset == startOfLastLine + columnAfterMovingLeft);
}

TEST_CASE("Up and down respect tab stops rather than byte counts")
{
	const TextBuffer buffer{"\tx\n    y"};
	const std::size_t offsetOfXAtColumnFour = 1;
	const std::size_t startOfSecondLine = 3;
	const std::size_t byteOfYAtColumnFour = 4;

	Caret caret = CaretWithoutDesiredColumnAt(offsetOfXAtColumnFour);
	caret = Step(buffer, caret, Motion::Down);

	CHECK(caret.Offset == startOfSecondLine + byteOfYAtColumnFour);
}

TEST_CASE("Up on the first line and down on the last stay put")
{
	const TextBuffer buffer{"one\ntwo"};

	CHECK(Step(buffer, CaretWithoutDesiredColumnAt(1), Motion::Up).Offset == 1);
	CHECK(Step(buffer, CaretWithoutDesiredColumnAt(5), Motion::Down).Offset == 5);
}

TEST_CASE("Page movement covers a screen and clamps at the ends")
{
	const std::size_t lineCount = 50;
	std::string content;

	for (std::size_t lineIndex = 0; lineIndex < lineCount; ++lineIndex)
	{
		content += "line\n";
	}

	const TextBuffer buffer{content};
	const SelectionChange selectionChange = SelectionChange::Collapse;

	const Caret start = CaretWithoutDesiredColumnAt(buffer.LineStart(20));
	CHECK(Move(buffer, start, Motion::PageDown, selectionChange, kTabWidth, kPageLines).Offset == buffer.LineStart(30));
	CHECK(Move(buffer, start, Motion::PageUp, selectionChange, kTabWidth, kPageLines).Offset == buffer.LineStart(10));

	CHECK(Move(buffer, CaretWithoutDesiredColumnAt(0), Motion::PageUp, selectionChange, kTabWidth, kPageLines).Offset == 0);

	const Caret last = CaretWithoutDesiredColumnAt(buffer.LineStart(buffer.GetLineCount() - 1));
	CHECK(Move(buffer, last, Motion::PageDown, selectionChange, kTabWidth, kPageLines).Offset == last.Offset);
}

TEST_CASE("Word movement crosses runs of like characters")
{
	const TextBuffer buffer{"int value_2 = other;"};
	const std::size_t endOfInt = 3;
	const std::size_t startOfValue = 4;
	const std::size_t endOfValue = 11;

	CHECK(Step(buffer, CaretWithoutDesiredColumnAt(0), Motion::WordRight).Offset == endOfInt);
	CHECK(Step(buffer, CaretWithoutDesiredColumnAt(endOfInt), Motion::WordRight).Offset == startOfValue);
	CHECK(Step(buffer, CaretWithoutDesiredColumnAt(startOfValue), Motion::WordRight).Offset == endOfValue);

	CHECK(Step(buffer, CaretWithoutDesiredColumnAt(endOfValue), Motion::WordLeft).Offset == startOfValue);
	CHECK(Step(buffer, CaretWithoutDesiredColumnAt(startOfValue), Motion::WordLeft).Offset == endOfInt);
}

TEST_CASE("Word movement stops at the ends of the document")
{
	const TextBuffer buffer{"word"};

	CHECK(Step(buffer, CaretWithoutDesiredColumnAt(0), Motion::WordLeft).Offset == 0);
	CHECK(Step(buffer, CaretWithoutDesiredColumnAt(4), Motion::WordRight).Offset == 4);
}

TEST_CASE("Shift keeps the anchor, so movement extends the selection")
{
	const TextBuffer buffer{"abcdef"};

	Caret caret = CaretWithoutDesiredColumnAt(2);
	caret = StepExtendingSelection(buffer, caret, Motion::Right);
	caret = StepExtendingSelection(buffer, caret, Motion::Right);

	CHECK(caret.Anchor == 2);
	CHECK(caret.Offset == 4);
	CHECK(SelectionRange(caret) == OffsetRange{.From = 2, .To = 4});
}

TEST_CASE("Movement without Shift collapses the selection")
{
	const TextBuffer buffer{"abcdef"};

	const Caret selected{.Offset = 4, .Anchor = 2, .DesiredColumn = 0};
	const Caret moved = Step(buffer, selected, Motion::Right);

	CHECK_FALSE(HasSelection(moved));
	CHECK(moved.Anchor == moved.Offset);
}

TEST_CASE("A click lands on a character boundary and never inside one")
{
	const TextBuffer buffer{"a" + kLetterAWithOgonek + "b"};
	const std::size_t middleOfLetterAWithOgonek = 2;
	const std::size_t pastTheEnd = 99;

	CHECK(PlaceAt(buffer, Caret{}, middleOfLetterAWithOgonek, SelectionChange::Collapse, kTabWidth).Offset == 1);
	CHECK(PlaceAt(buffer, Caret{}, pastTheEnd, SelectionChange::Collapse, kTabWidth).Offset == buffer.GetLength());
}

TEST_CASE("A click with Shift extends from the existing anchor")
{
	const TextBuffer buffer{"abcdef"};
	const Caret anchoredAtTwo{.Offset = 2, .Anchor = 2, .DesiredColumn = 0};
	const std::size_t clickedOffset = 5;

	const Caret placed = PlaceAt(buffer, anchoredAtTwo, clickedOffset, SelectionChange::Extend, kTabWidth);

	CHECK(placed.Anchor == 2);
	CHECK(placed.Offset == 5);
}

TEST_CASE("Double click selects the word under the caret")
{
	const TextBuffer buffer{"int value_2 = other;"};

	const Caret word = SelectWordAt(buffer, 6);

	CHECK(SelectionRange(word) == OffsetRange{.From = 4, .To = 11});
}

TEST_CASE("Double click on whitespace selects the run of whitespace")
{
	const TextBuffer buffer{"a    b"};

	CHECK(SelectWordAt(buffer, 2).Offset == 5);
	CHECK(SelectionRange(SelectWordAt(buffer, 2)) == OffsetRange{.From = 1, .To = 5});
}

TEST_CASE("Non-ASCII bytes count as word characters, so a Polish word is one word")
{
	const TextBuffer buffer{"cz\xC4\x99\xC5\x9B\xC4\x87 next"};
	const std::size_t endOfPolishWord = 8;

	CHECK(SelectionRange(SelectWordAt(buffer, 0)).To == endOfPolishWord);
}

TEST_CASE("Triple click selects the line with its ending")
{
	const TextBuffer buffer{"one\ntwo\nthree"};

	CHECK(SelectionRange(SelectLineAt(buffer, 1)) == OffsetRange{.From = 4, .To = 8});
}

TEST_CASE("The last line has no ending to include")
{
	const TextBuffer buffer{"one\ntwo"};

	CHECK(SelectionRange(SelectLineAt(buffer, 1)) == OffsetRange{.From = 4, .To = buffer.GetLength()});
}

TEST_CASE("Select all covers the document")
{
	const TextBuffer buffer{"one\ntwo"};

	const Caret all = SelectAll(buffer);

	CHECK(SelectionRange(all) == OffsetRange{.From = 0, .To = buffer.GetLength()});
}

TEST_CASE("Every motion leaves the caret on a character boundary")
{
	const TextBuffer buffer{"\t" + kLetterAWithOgonek + kSlightlySmilingFace + " word\n" + kLetterEWithOgonek + "nd\nx"};
	const std::size_t threeLinePage = 3;

	const Motion motions[] = {Motion::Left, Motion::Right, Motion::Up,
							  Motion::Down, Motion::LineStart, Motion::LineEnd,
							  Motion::WordLeft, Motion::WordRight, Motion::PageUp,
							  Motion::PageDown, Motion::DocumentStart, Motion::DocumentEnd};

	for (const Motion motion : motions)
	{
		for (std::size_t startOffset = 0; startOffset <= buffer.GetLength(); ++startOffset)
		{
			const Caret result = Move(buffer, CaretWithoutDesiredColumnAt(startOffset), motion, SelectionChange::Collapse, kTabWidth, threeLinePage);

			REQUIRE(result.Offset <= buffer.GetLength());

			if (result.Offset < buffer.GetLength())
			{
				REQUIRE(!IsUtf8ContinuationByte(buffer.CharAt(result.Offset)));
			}
		}
	}
}

TEST_CASE("Left and right step characters across piece boundaries when each character sits in a piece of its own")
{
	TextBuffer buffer{"ab"};

	buffer.Insert(1, kLetterEWithOgonek);
	buffer.Insert(1, kLetterAWithOgonek);

	REQUIRE(buffer.Materialize() == "a\xC4\x85\xC4\x99"
									"b");

	Caret caret = CaretWithoutDesiredColumnAt(0);

	caret = Step(buffer, caret, Motion::Right);
	CHECK(caret.Offset == 1);

	caret = Step(buffer, caret, Motion::Right);
	CHECK(caret.Offset == 3);

	caret = Step(buffer, caret, Motion::Right);
	CHECK(caret.Offset == 5);

	caret = Step(buffer, caret, Motion::Left);
	CHECK(caret.Offset == 3);

	caret = Step(buffer, caret, Motion::Left);
	CHECK(caret.Offset == 1);
}

TEST_CASE("A buffer offset backs off to a character boundary and clamps to the end")
{
	const TextBuffer buffer{"a" + kLetterEWithOgonek};

	CHECK(CharacterBoundaryAtOrBefore(buffer, 2) == 1);
	CHECK(CharacterBoundaryAtOrBefore(buffer, 3) == 3);
	CHECK(CharacterBoundaryAtOrBefore(buffer, std::numeric_limits<std::size_t>::max()) == 3);
	CHECK(CharacterBoundaryAtOrBefore(TextBuffer{}, 4) == 0);
	CHECK(CharacterBoundaryAtOrBefore(TextBuffer{std::string{"\x80\x80"}}, 1) == 0);
}
