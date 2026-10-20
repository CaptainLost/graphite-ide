#include "Core/Text/Editing.h"

#include "Core/Text/Columns.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/Indent.h"
#include "Core/Text/RowMap.h"
#include "Core/Text/TextBuffer.h"
#include "Core/Text/TextDocument.h"

#include "TempDirectory.h"

#include <doctest/doctest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <string>

namespace {
	constexpr Core::IndentStyle kFourSpaces{.UseSpaces = true, .Width = 4};
	constexpr Core::IndentStyle kTabs{.UseSpaces = false, .Width = 4};
	constexpr Core::IndentStyle kAnyIndentStyle{};

	Core::OpenItem ItemWith(std::string content)
	{
		Core::OpenItem item;

		Core::Result<Core::TextDocument> document = Core::TextDocument::FromContent(std::move(content), "test.txt");
		REQUIRE(document);

		item.Document = std::make_shared<Core::TextDocument>(std::move(document).GetValue());
		Core::RecomputeLineCaches(item);

		return item;
	}

	void PlaceCaretAt(Core::OpenItem& item, std::size_t offset)
	{
		item.Caret.Offset = offset;
		item.Caret.Anchor = offset;
	}

	void SelectFromTo(Core::OpenItem& item, std::size_t anchor, std::size_t offset)
	{
		item.Caret.Anchor = anchor;
		item.Caret.Offset = offset;
	}

	// Edits, Undo and Redo all catch the caches up incrementally (CatchUpWithDocument), recomputing in full only when an
	// edit touches the widest line, so this compares the incremental result with a fresh recomputation.
	void CheckLineCachesAreCorrect(const Core::OpenItem& item)
	{
		const Core::TextBuffer& buffer = item.Document->GetBuffer();

		CHECK(item.WidestColumn == Core::WidestColumn(buffer, Core::DefaultTabWidth));
		CHECK(item.IndentDepths == Core::IndentDepths(buffer, Core::DefaultTabWidth));

		Core::RowMap freshRows;
		freshRows.Rebuild(buffer, item.Rows.GetColumnsPerRow(), Core::DefaultTabWidth);

		REQUIRE(item.Rows.GetLineCount() == buffer.GetLineCount());
		CHECK(item.Rows.GetLineCount() == freshRows.GetLineCount());
		CHECK(item.Rows.GetTotalRows() == freshRows.GetTotalRows());

		for (std::size_t line = 0; line < buffer.GetLineCount(); ++line)
		{
			CAPTURE(line);
			CHECK(item.Rows.RowsOf(line) == freshRows.RowsOf(line));
			CHECK(item.Rows.FirstRowOf(line) == freshRows.FirstRowOf(line));
			CHECK(item.Rows.BreaksOf(line) == freshRows.BreaksOf(line));
		}
	}
}

TEST_CASE("Typing inserts at the caret")
{
	Core::OpenItem item = ItemWith("ac");
	const std::size_t betweenAAndC = 1;
	PlaceCaretAt(item, betweenAAndC);

	CHECK(Core::InsertText(item, "b"));
	CHECK(item.Document->GetBuffer().Materialize() == "abc");
	CHECK(item.Caret.Offset == 2);
	CheckLineCachesAreCorrect(item);
}

TEST_CASE("Typing replaces the selection")
{
	Core::OpenItem item = ItemWith("one two three");
	const std::size_t startOfTwo = 4;
	const std::size_t endOfTwo = 7;
	SelectFromTo(item, startOfTwo, endOfTwo);

	CHECK(Core::InsertText(item, "X"));
	CHECK(item.Document->GetBuffer().Materialize() == "one X three");
	CHECK(item.Caret.Offset == 5);
	CHECK(item.Caret.Anchor == 5);
	CHECK(item.Document->GetBuffer().Undo().has_value());
	CHECK(item.Document->GetBuffer().Materialize() == "one two three");
}

TEST_CASE("Pasted CRLF is normalised before it reaches the buffer")
{
	Core::OpenItem item = ItemWith("");

	CHECK(Core::InsertText(item, "a\r\nb\rc"));
	CHECK(item.Document->GetBuffer().Materialize() == "a\nb\nc");
	CHECK(item.Document->GetBuffer().GetLineCount() == 3);
	CheckLineCachesAreCorrect(item);
}

TEST_CASE("Backspace removes the character before the caret")
{
	Core::OpenItem item = ItemWith("abc");
	const std::size_t betweenBAndC = 2;
	PlaceCaretAt(item, betweenBAndC);

	CHECK(Core::DeleteBackward(item));
	CHECK(item.Document->GetBuffer().Materialize() == "ac");
	CHECK(item.Caret.Offset == 1);
	CheckLineCachesAreCorrect(item);
}

TEST_CASE("Backspace removes both bytes of a two-byte UTF-8 character")
{
	const std::string twoTwoByteCharacters = "\xc4\x85\xc4\x87";
	Core::OpenItem item = ItemWith(twoTwoByteCharacters);
	const std::size_t endOfBothCharacters = 4;
	PlaceCaretAt(item, endOfBothCharacters);

	CHECK(Core::DeleteBackward(item));
	CHECK(item.Document->GetBuffer().Materialize() == "\xc4\x85");
}

TEST_CASE("Backspace joins two lines and the caches follow")
{
	Core::OpenItem item = ItemWith("    one\n    two");
	const std::size_t startOfSecondLine = 8;
	PlaceCaretAt(item, startOfSecondLine);

	CHECK(Core::DeleteBackward(item));
	CHECK(item.Document->GetBuffer().Materialize() == "    one    two");
	CHECK(item.Document->GetBuffer().GetLineCount() == 1);
	CheckLineCachesAreCorrect(item);
}

TEST_CASE("Backspace with a selection removes the selection")
{
	Core::OpenItem item = ItemWith("one two three");
	const std::size_t startOfTwo = 4;
	const std::size_t startOfThree = 8;
	SelectFromTo(item, startOfTwo, startOfThree);

	CHECK(Core::DeleteBackward(item));
	CHECK(item.Document->GetBuffer().Materialize() == "one three");
	CHECK(item.Caret.Offset == startOfTwo);
}

TEST_CASE("Delete removes the character after the caret")
{
	Core::OpenItem item = ItemWith("abc");
	const std::size_t betweenAAndB = 1;
	PlaceCaretAt(item, betweenAAndB);

	CHECK(Core::DeleteForward(item));
	CHECK(item.Document->GetBuffer().Materialize() == "ac");
	CHECK(item.Caret.Offset == betweenAAndB);
}

TEST_CASE("Word deletion removes one run of letters in either direction and leaves the spaces around it")
{
	Core::OpenItem backward = ItemWith("one two three");
	const std::size_t endOfTwo = 7;
	PlaceCaretAt(backward, endOfTwo);

	CHECK(Core::DeleteWordBackward(backward));
	CHECK(backward.Document->GetBuffer().Materialize() == "one  three");

	Core::OpenItem forward = ItemWith("one two three");
	const std::size_t startOfTwo = 4;
	PlaceCaretAt(forward, startOfTwo);

	CHECK(Core::DeleteWordForward(forward));
	CHECK(forward.Document->GetBuffer().Materialize() == "one  three");
}

TEST_CASE("Deleting at the ends of the document changes nothing")
{
	Core::OpenItem caretAtStart = ItemWith("abc");
	CHECK_FALSE(Core::DeleteBackward(caretAtStart));
	CHECK(caretAtStart.Document->GetBuffer().Materialize() == "abc");

	Core::OpenItem caretAtEnd = ItemWith("abc");
	const std::size_t endOfText = 3;
	PlaceCaretAt(caretAtEnd, endOfText);
	CHECK_FALSE(Core::DeleteForward(caretAtEnd));
	CHECK(caretAtEnd.Document->GetBuffer().Materialize() == "abc");
}

TEST_CASE("Deleting the widest line shrinks widestColumn back down")
{
	Core::OpenItem item = ItemWith("short\nmuch much longer line\nshort");
	const std::size_t startOfWidestLine = 6;
	const std::size_t startOfLastLine = 28;
	SelectFromTo(item, startOfWidestLine, startOfLastLine);

	CHECK(Core::DeleteBackward(item));
	CheckLineCachesAreCorrect(item);
	CHECK(item.WidestColumn == 5);
}

TEST_CASE("A document holding invalid UTF-8 opens read-only and refuses every edit")
{
	const std::string withLoneContinuationByte = "ab\x80xy";
	Core::OpenItem item = ItemWith(withLoneContinuationByte);
	REQUIRE(item.Document->IsReadOnly());

	CHECK_FALSE(Core::InsertText(item, "z"));
	CHECK_FALSE(Core::DeleteBackward(item));
	CHECK_FALSE(Core::DeleteForward(item));
	CHECK(item.Document->GetBuffer().Materialize() == "ab\x80xy");
}

TEST_CASE("A reload made without asking that turns the document read-only can still be undone and redone")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path samplePath = temporaryDirectory.MakeFile("sample.txt", "hello\n");

	Core::Result<Core::TextDocument> loaded = Core::TextDocument::Load(samplePath);
	REQUIRE(loaded);

	Core::OpenItem item;
	item.Document = std::make_shared<Core::TextDocument>(std::move(loaded).GetValue());
	Core::RecomputeLineCaches(item);
	REQUIRE_FALSE(item.Document->IsReadOnly());

	{
		const std::string latin1WithLoneContinuationByte = "ab\x80xy\n";
		std::ofstream stream{samplePath, std::ios::binary | std::ios::trunc};
		stream.write(latin1WithLoneContinuationByte.data(), static_cast<std::streamsize>(latin1WithLoneContinuationByte.size()));
	}

	REQUIRE(item.Document->ReloadFromDisk());
	REQUIRE(item.Document->IsReadOnly());
	REQUIRE(item.Document->GetBuffer().Materialize() == "ab\x80xy\n");

	CHECK(Core::Undo(item));
	CHECK(item.Document->GetBuffer().Materialize() == "hello\n");
	CheckLineCachesAreCorrect(item);

	CHECK(item.Document->EncodeToBytes() == "hello\n");

	CHECK(Core::Redo(item));
	CHECK(item.Document->GetBuffer().Materialize() == "ab\x80xy\n");
	CHECK(item.Document->EncodeToBytes() == "ab\x80xy\n");

	CHECK_FALSE(Core::InsertText(item, "z"));
	CHECK_FALSE(Core::InsertNewline(item));
}

TEST_CASE("A line inserted below the widest line keeps the indent depth of the content it pushes down")
{
	Core::OpenItem item = ItemWith("if xxxxxxxxxx:\n    body1\n    body2");
	REQUIRE(item.WidestLine == 0);
	REQUIRE(item.IndentDepths == std::vector<std::size_t>{0, 4, 4});

	const std::size_t startOfIndentedBody1 = 15;
	PlaceCaretAt(item, startOfIndentedBody1);

	CHECK(Core::InsertText(item, "\n"));
	CHECK(item.Document->GetBuffer().Materialize() == "if xxxxxxxxxx:\n\n    body1\n    body2");

	CHECK(item.IndentDepths == std::vector<std::size_t>{0, 0, 4, 4});
	CHECK(item.WidestLine == 0);
	CheckLineCachesAreCorrect(item);
}

TEST_CASE("A line-joining backspace below the widest line keeps the indent depth of content below it")
{
	Core::OpenItem item = ItemWith("if xxxxxxxxxx:\n    one\n\n        two");
	REQUIRE(item.WidestLine == 0);
	REQUIRE(item.IndentDepths == std::vector<std::size_t>{0, 4, 4, 8});

	const std::size_t startOfBlankLine = 23;
	PlaceCaretAt(item, startOfBlankLine);

	CHECK(Core::DeleteBackward(item));
	CHECK(item.Document->GetBuffer().Materialize() == "if xxxxxxxxxx:\n    one\n        two");

	CHECK(item.IndentDepths == std::vector<std::size_t>{0, 4, 8});
	CHECK(item.WidestLine == 0);
	CheckLineCachesAreCorrect(item);
}

TEST_CASE("Undo through Editing restores the caret and the caches")
{
	Core::OpenItem item = ItemWith("short\nlonger line here");
	const std::size_t endOfText = 22;
	PlaceCaretAt(item, endOfText);

	CHECK(Core::InsertText(item, " and more"));
	const std::size_t widestColumnAfterTyping = item.WidestColumn;

	CHECK(Core::Undo(item));
	CHECK(item.Document->GetBuffer().Materialize() == "short\nlonger line here");
	CHECK(item.WidestColumn < widestColumnAfterTyping);
	CheckLineCachesAreCorrect(item);

	CHECK(Core::Redo(item));
	CHECK(item.WidestColumn == widestColumnAfterTyping);
	CheckLineCachesAreCorrect(item);
}

TEST_CASE("Enter carries the previous line's indentation")
{
	Core::OpenItem item = ItemWith("    if (x)");
	const std::size_t endOfLine = 10;
	PlaceCaretAt(item, endOfLine);

	CHECK(Core::InsertNewline(item));
	CHECK(item.Document->GetBuffer().Materialize() == "    if (x)\n    ");
	CHECK(item.Caret.Offset == 15);
	CheckLineCachesAreCorrect(item);
}

TEST_CASE("Enter in the middle of a line splits it and still indents")
{
	Core::OpenItem item = ItemWith("  ab");
	const std::size_t betweenAAndB = 3;
	PlaceCaretAt(item, betweenAAndB);

	CHECK(Core::InsertNewline(item));
	CHECK(item.Document->GetBuffer().Materialize() == "  a\n  b");
	CheckLineCachesAreCorrect(item);
}

TEST_CASE("Enter on a blank line adds no indentation of its own")
{
	Core::OpenItem item = ItemWith("");

	CHECK(Core::InsertNewline(item));
	CHECK(item.Document->GetBuffer().Materialize() == "\n");
	CheckLineCachesAreCorrect(item);
}

TEST_CASE("Tab without a selection inserts one indent")
{
	Core::OpenItem spaces = ItemWith("ab");
	const std::size_t startOfText = 0;
	PlaceCaretAt(spaces, startOfText);

	CHECK(Core::Indent(spaces, kFourSpaces));
	CHECK(spaces.Document->GetBuffer().Materialize() == "    ab");
	CheckLineCachesAreCorrect(spaces);

	Core::OpenItem tabs = ItemWith("ab");
	CHECK(Core::Indent(tabs, kTabs));
	CHECK(tabs.Document->GetBuffer().Materialize() == "\tab");
	CheckLineCachesAreCorrect(tabs);
}

TEST_CASE("Tab indents every line the selection touches, as one undo step")
{
	Core::OpenItem item = ItemWith("one\ntwo\nthree");
	const std::size_t insideOne = 1;
	const std::size_t insideTwo = 6;
	SelectFromTo(item, insideOne, insideTwo);

	CHECK(Core::Indent(item, kFourSpaces));
	CHECK(item.Document->GetBuffer().Materialize() == "    one\n    two\nthree");
	CheckLineCachesAreCorrect(item);

	CHECK(item.Document->GetBuffer().Undo().has_value());
	CHECK(item.Document->GetBuffer().Materialize() == "one\ntwo\nthree");
}

TEST_CASE("A selection ending at the start of a line does not indent that line")
{
	Core::OpenItem item = ItemWith("one\ntwo\nthree");
	const std::size_t startOfText = 0;
	const std::size_t startOfThree = 8;
	SelectFromTo(item, startOfText, startOfThree);

	CHECK(Core::Indent(item, kFourSpaces));
	CHECK(item.Document->GetBuffer().Materialize() == "    one\n    two\nthree");
	CheckLineCachesAreCorrect(item);
}

TEST_CASE("Shift+Tab removes one indent and never more than exists")
{
	Core::OpenItem item = ItemWith("        deep\n  shallow\nnone");
	const std::size_t startOfText = 0;
	const std::size_t endOfText = item.Document->GetBuffer().GetLength();
	SelectFromTo(item, startOfText, endOfText);

	CHECK(Core::Outdent(item, kFourSpaces));
	CHECK(item.Document->GetBuffer().Materialize() == "    deep\nshallow\nnone");
	CheckLineCachesAreCorrect(item);
}

TEST_CASE("Shift+Tab with nothing to remove changes nothing")
{
	Core::OpenItem item = ItemWith("none");
	CHECK_FALSE(Core::Outdent(item, kFourSpaces));
	CHECK(item.Document->GetBuffer().Materialize() == "none");
}

TEST_CASE("Shift+Tab with no selection removes the indent of just the caret's line")
{
	Core::OpenItem item = ItemWith("    ab");
	CHECK(Core::Outdent(item, kFourSpaces));
	CHECK(item.Document->GetBuffer().Materialize() == "ab");
	CheckLineCachesAreCorrect(item);
}

TEST_CASE("Tab with no selection is its own undo step like the block form so Ctrl+Z leaves the typing before it")
{
	Core::OpenItem item = ItemWith("");

	CHECK(Core::InsertText(item, "ab"));
	CHECK(Core::Indent(item, kFourSpaces));
	CHECK(item.Document->GetBuffer().Materialize() == "ab    ");

	CHECK(Core::Undo(item));
	CHECK(item.Document->GetBuffer().Materialize() == "ab");
}

TEST_CASE("Shift+Tab with nothing to remove does not break the undo group")
{
	Core::OpenItem item = ItemWith("");

	CHECK(Core::InsertText(item, "ab"));
	CHECK_FALSE(Core::Outdent(item, kFourSpaces));
	CHECK(Core::InsertText(item, "cd"));
	CHECK(item.Document->GetBuffer().Materialize() == "abcd");

	CHECK(Core::Undo(item));
	CHECK(item.Document->GetBuffer().Materialize() == "");
}

TEST_CASE("A read-only document refuses newline and indentation")
{
	const std::string withLoneContinuationByte = "ab\x80xy";
	Core::OpenItem item = ItemWith(withLoneContinuationByte);
	REQUIRE(item.Document->IsReadOnly());

	CHECK_FALSE(Core::InsertNewline(item));
	CHECK_FALSE(Core::Indent(item, kAnyIndentStyle));
	CHECK_FALSE(Core::Outdent(item, kAnyIndentStyle));
}

TEST_CASE("InsertNewline below the widest line splices indentDepths instead of "
		  "misaligning the tail")
{
	Core::OpenItem item = ItemWith("aaaaaaaaaa\nb\ncd");
	REQUIRE(item.WidestLine == 0);

	const std::size_t betweenCAndD = 14;
	PlaceCaretAt(item, betweenCAndD);
	REQUIRE(item.Document->GetBuffer().ToPosition(item.Caret.Offset).Line == 2);

	CHECK(Core::InsertNewline(item));
	CHECK(item.Document->GetBuffer().Materialize() == "aaaaaaaaaa\nb\nc\nd");
	CHECK(item.WidestLine == 0);
	CheckLineCachesAreCorrect(item);
}

TEST_CASE("InsertNewline below the widest line repairs the depths across a run of blank lines up to the next non-blank line")
{
	Core::OpenItem item = ItemWith("xxxxxxxxxxxxxxxx\n    if\n\n\n        end");
	REQUIRE(item.WidestLine == 0);

	const std::size_t endOfIf = 23;
	PlaceCaretAt(item, endOfIf);
	REQUIRE(item.Document->GetBuffer().ToPosition(item.Caret.Offset).Line == 1);

	CHECK(Core::InsertNewline(item));
	CHECK(item.Document->GetBuffer().Materialize() == "xxxxxxxxxxxxxxxx\n    if\n    \n\n\n        end");
	CHECK(item.WidestLine == 0);
	CheckLineCachesAreCorrect(item);
}

TEST_CASE("Indent on a block below the widest line takes the incremental path")
{
	Core::OpenItem item = ItemWith("xxxxxxxxxxxxxxxx\none\ntwo");
	REQUIRE(item.WidestLine == 0);

	const std::size_t startOfOne = 17;
	const std::size_t endOfTwo = 24;
	SelectFromTo(item, startOfOne, endOfTwo);
	REQUIRE(item.Document->GetBuffer().ToPosition(item.Caret.Anchor).Line == 1);

	CHECK(Core::Indent(item, kFourSpaces));
	CHECK(item.Document->GetBuffer().Materialize() == "xxxxxxxxxxxxxxxx\n    one\n    two");
	CHECK(item.WidestLine == 0);
	CheckLineCachesAreCorrect(item);
}

TEST_CASE("Outdenting a line below a blank line updates the blank line's indent depth")
{
	Core::OpenItem item = ItemWith("    a\n\n    b");
	REQUIRE(item.IndentDepths.size() == 3);

	const std::size_t startOfB = 7;
	const std::size_t endOfText = 12;
	SelectFromTo(item, startOfB, endOfText);
	REQUIRE(item.Document->GetBuffer().ToPosition(item.Caret.Anchor).Line == 2);

	CHECK(Core::Outdent(item, kFourSpaces));
	CHECK(item.Document->GetBuffer().Materialize() == "    a\n\nb");
	CHECK(item.IndentDepths[1] == 0);
	CheckLineCachesAreCorrect(item);
}

TEST_CASE("Outdent on a block below the widest line takes the incremental path")
{
	Core::OpenItem item = ItemWith("xxxxxxxxxxxxxxxx\n    one\n    two");
	REQUIRE(item.WidestLine == 0);

	const std::size_t startOfIndentedOne = 17;
	const std::size_t endOfText = 32;
	SelectFromTo(item, startOfIndentedOne, endOfText);
	REQUIRE(item.Document->GetBuffer().ToPosition(item.Caret.Anchor).Line == 1);

	CHECK(Core::Outdent(item, kFourSpaces));
	CHECK(item.Document->GetBuffer().Materialize() == "xxxxxxxxxxxxxxxx\none\ntwo");
	CHECK(item.WidestLine == 0);
	CheckLineCachesAreCorrect(item);
}

namespace {
	constexpr std::size_t kWrapColumns = 10;

	// The three lines take different numbers of rows on purpose, so a splice landing against the wrong line changes a count.
	const std::string kLinesWrappingToThreeTwoAndOneRows = "aaaaaaaaaaaaaaaaaaaaaaaaa\nbbbb cccc dddd eeee\nff";

	Core::OpenItem WrappedItemWith(std::string content, std::size_t columnsPerRow)
	{
		Core::OpenItem item = ItemWith(std::move(content));

		item.Rows.Rebuild(item.Document->GetBuffer(), columnsPerRow, Core::DefaultTabWidth);

		return item;
	}
}

TEST_CASE("InsertNewline below the widest line splices the row map into line")
{
	Core::OpenItem item = WrappedItemWith(kLinesWrappingToThreeTwoAndOneRows, kWrapColumns);
	REQUIRE(item.WidestLine == 0);
	REQUIRE(item.Rows.GetLineCount() == 3);
	REQUIRE(item.Rows.RowsOf(0) == 3);
	REQUIRE(item.Rows.RowsOf(1) == 2);
	REQUIRE(item.Rows.RowsOf(2) == 1);

	const std::size_t rowsBefore = item.Rows.GetTotalRows();

	const std::size_t betweenTheTwoFs = 47;
	PlaceCaretAt(item, betweenTheTwoFs);
	REQUIRE(item.Document->GetBuffer().ToPosition(item.Caret.Offset).Line == 2);

	CHECK(Core::InsertNewline(item));
	CHECK(item.Document->GetBuffer().Materialize() == "aaaaaaaaaaaaaaaaaaaaaaaaa\nbbbb cccc dddd eeee\nf\nf");

	CHECK(item.Rows.GetColumnsPerRow() == kWrapColumns);
	CHECK(item.Rows.GetLineCount() == item.Document->GetBuffer().GetLineCount());
	CHECK(item.Rows.GetTotalRows() == rowsBefore + 1);
	CheckLineCachesAreCorrect(item);
}

TEST_CASE("Deleting a line break below the widest line splices the row map out again")
{
	Core::OpenItem item = WrappedItemWith(kLinesWrappingToThreeTwoAndOneRows, kWrapColumns);
	REQUIRE(item.WidestLine == 0);
	REQUIRE(item.Rows.GetLineCount() == 3);

	const std::size_t startOfLastLine = 46;
	PlaceCaretAt(item, startOfLastLine);
	REQUIRE(item.Document->GetBuffer().ToPosition(item.Caret.Offset).Line == 2);

	CHECK(Core::DeleteBackward(item));
	CHECK(item.Document->GetBuffer().Materialize() == "aaaaaaaaaaaaaaaaaaaaaaaaa\nbbbb cccc dddd eeeeff");
	CHECK(item.Rows.GetColumnsPerRow() == kWrapColumns);
	CHECK(item.Rows.GetLineCount() == 2);
	CHECK(item.Rows.RowsOf(0) == 3);
	CHECK(item.Rows.RowsOf(1) == 3);
	CheckLineCachesAreCorrect(item);
}

TEST_CASE("Undo keeps the map wrapping to the width it was built for rather than unwrapping it until the next resize")
{
	Core::OpenItem item = WrappedItemWith(kLinesWrappingToThreeTwoAndOneRows, kWrapColumns);
	const std::size_t rowsBefore = item.Rows.GetTotalRows();

	const std::size_t endOfText = 48;
	PlaceCaretAt(item, endOfText);
	CHECK(Core::InsertText(item, " gggg hhhh"));
	CHECK(item.Rows.GetTotalRows() == rowsBefore + 1);

	CHECK(Core::Undo(item));
	CHECK(item.Document->GetBuffer().Materialize() == kLinesWrappingToThreeTwoAndOneRows);
	CHECK(item.Rows.GetColumnsPerRow() == kWrapColumns);
	CHECK(item.Rows.GetTotalRows() == rowsBefore);
	CheckLineCachesAreCorrect(item);
}

TEST_CASE("Undo puts the caret where the change it reversed was")
{
	Core::OpenItem item = ItemWith("abc");

	REQUIRE(Core::InsertText(item, "XYZ"));
	REQUIRE(item.Document->GetBuffer().Materialize() == "XYZabc");

	const std::size_t endOfText = 6;
	PlaceCaretAt(item, endOfText);

	REQUIRE(Core::Undo(item));

	CHECK(item.Document->GetBuffer().Materialize() == "abc");
	CHECK(item.Caret.Offset == 0);
	CHECK(item.Caret.Anchor == 0);
}

TEST_CASE("Redo puts the caret after the change it reapplied")
{
	Core::OpenItem item = ItemWith("abc");

	REQUIRE(Core::InsertText(item, "XYZ"));
	REQUIRE(Core::Undo(item));

	const std::size_t endOfAbc = 3;
	PlaceCaretAt(item, endOfAbc);

	REQUIRE(Core::Redo(item));

	CHECK(item.Document->GetBuffer().Materialize() == "XYZabc");
	CHECK(item.Caret.Offset == 3);
}

namespace {
	void CheckCachesMatchRecompute(const Core::OpenItem& item)
	{
		Core::OpenItem fresh = item;
		Core::RecomputeLineCaches(fresh);

		CHECK(item.IndentDepths == fresh.IndentDepths);
		CHECK(item.WidestColumn == fresh.WidestColumn);
		REQUIRE(item.Rows.GetLineCount() == fresh.Rows.GetLineCount());
		CHECK(item.Rows.GetTotalRows() == fresh.Rows.GetTotalRows());

		for (std::size_t line = 0; line < fresh.Rows.GetLineCount(); ++line)
		{
			CHECK(item.Rows.RowsOf(line) == fresh.Rows.RowsOf(line));
		}
	}
}

TEST_CASE("A tab catches up with an edit made to its document from elsewhere")
{
	Core::OpenItem item = ItemWith("first\n    second\nthird");
	PlaceCaretAt(item, 12);

	item.Document->GetBuffer().Insert(0, "new line\n");
	Core::CatchUpWithDocument(item, Core::CaretFollow::Shift);

	CHECK(item.Caret.Offset == 21);
	CHECK(item.Caret.Anchor == 21);
	CHECK(item.SeenChange == item.Document->GetBuffer().GetChangeSequence());
	CheckCachesMatchRecompute(item);
}

TEST_CASE("A caret inside text deleted elsewhere lands where the deletion started")
{
	Core::OpenItem item = ItemWith("abcdefgh");
	PlaceCaretAt(item, 5);

	item.Document->GetBuffer().Erase(2, 7);
	Core::CatchUpWithDocument(item, Core::CaretFollow::Shift);

	CHECK(item.Caret.Offset == 2);
}

TEST_CASE("Catching up after any run of edits leaves the caches as a recompute would")
{
	std::mt19937 random{1234};
	const std::string pieces[] = {"x", "\n", "    ", "\t\n", "word\n  deeper\n", ""};
	Core::OpenItem item = ItemWith("start\n  indented\n\nend");

	for (int step = 0; step < 300; ++step)
	{
		Core::TextBuffer& buffer = item.Document->GetBuffer();
		const std::size_t length = buffer.GetLength();
		const int choice = static_cast<int>(random() % 5);

		if (choice == 0 && length > 0)
		{
			const std::size_t from = random() % length;
			buffer.Erase(from, from + 1 + random() % 4);
		}
		else if (choice == 1)
		{
			buffer.Undo();
		}
		else if (choice == 2)
		{
			buffer.Redo();
		}
		else
		{
			buffer.Insert(random() % (length + 1), pieces[random() % std::size(pieces)]);
		}

		Core::CatchUpWithDocument(item, Core::CaretFollow::Shift);
		CheckCachesMatchRecompute(item);
	}
}

TEST_CASE("Undo right after a reload leaves the caches as a recompute would")
{
	Core::OpenItem item = ItemWith("a\n  b\nc");
	PlaceCaretAt(item, 4);

	item.Document->GetBuffer().ReplaceContent("x\n\n    y\nz\nw");
	Core::CatchUpWithDocument(item, Core::CaretFollow::Shift);
	CheckCachesMatchRecompute(item);

	REQUIRE(Core::Undo(item));
	CheckCachesMatchRecompute(item);
	CHECK(item.Document->GetBuffer().Materialize() == "a\n  b\nc");
}

TEST_CASE("Indenting more lines than the change log keeps leaves the caches whole")
{
	std::string text;

	for (int line = 0; line < 3000; ++line)
	{
		text += "line\n";
	}

	Core::OpenItem item = ItemWith(text);
	SelectFromTo(item, 0, item.Document->GetBuffer().GetLength());

	REQUIRE(Core::Indent(item, kFourSpaces));

	CheckCachesMatchRecompute(item);
}

namespace {
	// The widest line is remembered by index, so every edit has to leave that index pointing at a line that is
	// still as wide as WidestColumn says.
	void CheckWidestLineIsWhereItIsRecorded(const Core::OpenItem& item)
	{
		const Core::TextBuffer& buffer = item.Document->GetBuffer();

		REQUIRE(item.WidestLine < buffer.GetLineCount());
		CHECK(Core::ColumnCount(buffer.LineText(item.WidestLine), item.TabWidth) == item.WidestColumn);
		CHECK(item.WidestColumn == Core::WidestColumn(buffer, item.TabWidth));
	}
}

TEST_CASE("An edit above the widest line that keeps the line count leaves the widest line where it is")
{
	Core::OpenItem item = ItemWith("a\nb\nthe widest line of all\nc");
	REQUIRE(item.WidestLine == 2);

	PlaceCaretAt(item, 1);

	REQUIRE(Core::InsertText(item, "x"));

	CHECK(item.WidestLine == 2);
	CheckWidestLineIsWhereItIsRecorded(item);
	CheckLineCachesAreCorrect(item);
}

TEST_CASE("A line inserted above the widest line moves the widest line down with it")
{
	Core::OpenItem item = ItemWith("a\nb\nthe widest line of all\nc");
	REQUIRE(item.WidestLine == 2);

	PlaceCaretAt(item, 0);

	REQUIRE(Core::InsertNewline(item));

	CHECK(item.WidestLine == 3);
	CheckWidestLineIsWhereItIsRecorded(item);
	CheckLineCachesAreCorrect(item);
}

TEST_CASE("Widening the widest line in place keeps it the widest line")
{
	Core::OpenItem item = ItemWith("a\nthe widest line of all\nc");
	REQUIRE(item.WidestLine == 1);

	const std::size_t endOfWidestLine = 24;
	PlaceCaretAt(item, endOfWidestLine);

	REQUIRE(Core::InsertText(item, " and then some"));

	CHECK(item.WidestLine == 1);
	CHECK(item.WidestColumn == 36);
	CheckWidestLineIsWhereItIsRecorded(item);
	CheckLineCachesAreCorrect(item);
}

TEST_CASE("Narrowing the widest line in place hands the title to the next widest line")
{
	Core::OpenItem item = ItemWith("a\nthe widest line of all\nthe second widest\nc");
	REQUIRE(item.WidestLine == 1);

	const std::size_t endOfWidestLine = 24;
	PlaceCaretAt(item, endOfWidestLine);

	for (int deleted = 0; deleted < 10; ++deleted)
	{
		REQUIRE(Core::DeleteBackward(item));
	}

	CHECK(item.WidestLine == 2);
	CheckWidestLineIsWhereItIsRecorded(item);
	CheckLineCachesAreCorrect(item);
}

TEST_CASE("Typing above and below the widest line over and over leaves the widest line recorded correctly")
{
	Core::OpenItem item = ItemWith("first\nsecond\nthe widest line of all\nfourth\nfifth");

	for (int round = 0; round < 20; ++round)
	{
		PlaceCaretAt(item, round % 2 == 0 ? 0 : item.Document->GetBuffer().GetLength());

		REQUIRE(Core::InsertText(item, "x"));

		CheckWidestLineIsWhereItIsRecorded(item);
	}

	CheckLineCachesAreCorrect(item);
}
