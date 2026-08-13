#include "Core/Text/Editing.h"

#include "Core/Text/Columns.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/Indent.h"
#include "Core/Text/RowMap.h"
#include "Core/Text/TextDocument.h"

#include "TempDirectory.h"

#include <doctest/doctest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
	Core::OpenItem ItemWith(std::string content)
	{
		Core::OpenItem item;

		auto document = Core::TextDocument::FromContent(std::move(content), "test.txt");
		REQUIRE(document);

		item.Document = std::move(document).GetValue();
		Core::RefreshDerived(item);

		return item;
	}

	void CheckDerivedCachesAreCorrect(const Core::OpenItem& item)
	{
		const Core::TextBuffer& buffer = item.Document->GetBuffer();

		CHECK(item.WidestColumn == Core::WidestColumn(buffer, Core::DefaultTabWidth));
		CHECK(item.IndentDepths == Core::IndentDepths(buffer, Core::DefaultTabWidth));

		Core::RowMap fresh;
		fresh.Rebuild(buffer, item.Rows.GetColumnsPerRow(), Core::DefaultTabWidth);

		REQUIRE(item.Rows.GetLineCount() == buffer.GetLineCount());
		CHECK(item.Rows.GetLineCount() == fresh.GetLineCount());
		CHECK(item.Rows.TotalRows() == fresh.TotalRows());

		for (std::size_t line = 0; line < buffer.GetLineCount(); ++line)
		{
			CAPTURE(line);
			CHECK(item.Rows.RowsOf(line) == fresh.RowsOf(line));
			CHECK(item.Rows.FirstRowOf(line) == fresh.FirstRowOf(line));
			CHECK(item.Rows.BreaksOf(line) == fresh.BreaksOf(line));
		}
	}
}

TEST_CASE("Typing inserts at the caret")
{
	Core::OpenItem item = ItemWith("ac");
	item.Caret.Offset = 1;
	item.Caret.Anchor = 1;

	CHECK(Core::InsertText(item, "b"));
	CHECK(item.Document->GetBuffer().Materialize() == "abc");
	CHECK(item.Caret.Offset == 2);
	CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("Typing replaces the selection")
{
	Core::OpenItem item = ItemWith("one two three");
	item.Caret.Anchor = 4;
	item.Caret.Offset = 7;

	CHECK(Core::InsertText(item, "X"));
	CHECK(item.Document->GetBuffer().Materialize() == "one X three");
	CHECK(item.Caret.Offset == 5);
	CHECK(item.Caret.Anchor == 5);
	CHECK(item.Document->GetBuffer().Undo());
	CHECK(item.Document->GetBuffer().Materialize() == "one two three");
}

TEST_CASE("Pasted CRLF is normalised before it reaches the buffer")
{
	Core::OpenItem item = ItemWith("");

	CHECK(Core::InsertText(item, "a\r\nb\rc"));
	CHECK(item.Document->GetBuffer().Materialize() == "a\nb\nc");
	CHECK(item.Document->GetBuffer().GetLineCount() == 3);
	CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("Backspace removes the character before the caret")
{
	Core::OpenItem item = ItemWith("abc");
	item.Caret.Offset = 2;
	item.Caret.Anchor = 2;

	CHECK(Core::DeleteBackward(item));
	CHECK(item.Document->GetBuffer().Materialize() == "ac");
	CHECK(item.Caret.Offset == 1);
	CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("Backspace removes a whole UTF-8 character")
{
	// "ąć" — two characters, four bytes. One backspace must take two of them.
	Core::OpenItem item = ItemWith("\xc4\x85\xc4\x87");
	item.Caret.Offset = 4;
	item.Caret.Anchor = 4;

	CHECK(Core::DeleteBackward(item));
	CHECK(item.Document->GetBuffer().Materialize() == "\xc4\x85");
}

TEST_CASE("Backspace joins two lines and the caches follow")
{
	Core::OpenItem item = ItemWith("    one\n    two");
	item.Caret.Offset = 8;
	item.Caret.Anchor = 8;

	CHECK(Core::DeleteBackward(item));
	CHECK(item.Document->GetBuffer().Materialize() == "    one    two");
	CHECK(item.Document->GetBuffer().GetLineCount() == 1);
	CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("Backspace with a selection removes the selection")
{
	Core::OpenItem item = ItemWith("one two three");
	item.Caret.Anchor = 4;
	item.Caret.Offset = 8;

	CHECK(Core::DeleteBackward(item));
	CHECK(item.Document->GetBuffer().Materialize() == "one three");
	CHECK(item.Caret.Offset == 4);
}

TEST_CASE("Delete removes the character after the caret")
{
	Core::OpenItem item = ItemWith("abc");
	item.Caret.Offset = 1;
	item.Caret.Anchor = 1;

	CHECK(Core::DeleteForward(item));
	CHECK(item.Document->GetBuffer().Materialize() == "ac");
	CHECK(item.Caret.Offset == 1);
}

TEST_CASE("Word deletion works in both directions")
{
	Core::OpenItem backward = ItemWith("one two three");
	backward.Caret.Offset = 7;
	backward.Caret.Anchor = 7;

	CHECK(Core::DeleteWordBackward(backward));
	// A single WordLeft hop covers only the run of letters, not the space beyond it —
	// CaretTests documents that motion as one class-run at a time — so backspacing off
	// the end of "two" removes exactly "two" and leaves both its neighbouring spaces.
	CHECK(backward.Document->GetBuffer().Materialize() == "one  three");

	Core::OpenItem forward = ItemWith("one two three");
	forward.Caret.Offset = 4;
	forward.Caret.Anchor = 4;

	CHECK(Core::DeleteWordForward(forward));
	CHECK(forward.Document->GetBuffer().Materialize() == "one  three");
}

TEST_CASE("Deleting at the ends of the document changes nothing")
{
	Core::OpenItem start = ItemWith("abc");
	CHECK_FALSE(Core::DeleteBackward(start));
	CHECK(start.Document->GetBuffer().Materialize() == "abc");

	Core::OpenItem end = ItemWith("abc");
	end.Caret.Offset = 3;
	end.Caret.Anchor = 3;
	CHECK_FALSE(Core::DeleteForward(end));
	CHECK(end.Document->GetBuffer().Materialize() == "abc");
}

TEST_CASE("Deleting the widest line shrinks widestColumn back down")
{
	Core::OpenItem item = ItemWith("short\nmuch much longer line\nshort");
	item.Caret.Anchor = 6;
	item.Caret.Offset = 28;

	CHECK(Core::DeleteBackward(item));
	CheckDerivedCachesAreCorrect(item);
	CHECK(item.WidestColumn == 5);
}

TEST_CASE("A read-only document refuses every edit")
{
	// Invalid UTF-8: a lone continuation byte. FromContent keeps the bytes and marks the
	// document read-only rather than refusing to open it.
	Core::OpenItem item = ItemWith("ab\x80xy");
	REQUIRE(item.Document->IsReadOnly());

	CHECK_FALSE(Core::InsertText(item, "z"));
	CHECK_FALSE(Core::DeleteBackward(item));
	CHECK_FALSE(Core::DeleteForward(item));
	CHECK(item.Document->GetBuffer().Materialize() == "ab\x80xy");
}

TEST_CASE("A reload that turns the document read-only can still be undone and redone")
{
	// The one class of reload where what is on screen genuinely changes character: a
	// clean UTF-8 file rewritten by another tool in some other encoding. The reload
	// happens without asking, and the design justifies that precisely by the reload
	// being undoable — so it has to be, even though the document is read-only afterwards.
	TempDirectory temp;
	const std::filesystem::path path = temp.MakeFile("sample.txt", "hello\n");

	auto loaded = Core::TextDocument::Load(path);
	REQUIRE(loaded);

	Core::OpenItem item;
	item.Document = std::move(loaded).GetValue();
	Core::RefreshDerived(item);
	REQUIRE_FALSE(item.Document->IsReadOnly());

	{
		// Latin-1: 0x80 is a lone continuation byte, so the content is no longer valid
		// UTF-8 and reloading marks the document read-only.
		const std::string latin1 = "ab\x80xy\n";
		std::ofstream stream{path, std::ios::binary | std::ios::trunc};
		stream.write(latin1.data(), static_cast<std::streamsize>(latin1.size()));
	}

	REQUIRE(item.Document->ReloadFromDisk());
	REQUIRE(item.Document->IsReadOnly());
	REQUIRE(item.Document->GetBuffer().Materialize() == "ab\x80xy\n");

	CHECK(Core::Undo(item));
	CHECK(item.Document->GetBuffer().Materialize() == "hello\n");
	CheckDerivedCachesAreCorrect(item);

	// And what comes back is writable bytes, not a mixture: undo restores a whole content
	// the buffer already held rather than inserting anything into the one on screen,
	// which is why the read-only flag has no business stopping it.
	CHECK(item.Document->EncodeToBytes() == "hello\n");

	CHECK(Core::Redo(item));
	CHECK(item.Document->GetBuffer().Materialize() == "ab\x80xy\n");
	CHECK(item.Document->EncodeToBytes() == "ab\x80xy\n");

	// Read-only still means read-only for everything that creates new content.
	CHECK_FALSE(Core::InsertText(item, "z"));
	CHECK_FALSE(Core::InsertNewline(item, {}));
}

TEST_CASE("A line inserted above unrelated content keeps that content's indent depth")
{
	// Regression test: indentDepths used to be patched with vector::resize, which pads
	// or truncates the *tail* rather than shifting entries at the edit site. Line 0 is
	// deliberately the widest line, so the edit below it takes the cheap incremental
	// path in UpdateDerived rather than a full RefreshDerived that would hide the bug.
	Core::OpenItem item = ItemWith("if xxxxxxxxxx:\n    body1\n    body2");
	REQUIRE(item.WidestLine == 0);
	REQUIRE(item.IndentDepths == std::vector<std::size_t>{0, 4, 4});

	// Caret at the start of "    body1" — pressing Enter right above it.
	item.Caret.Offset = 15;
	item.Caret.Anchor = 15;

	CHECK(Core::InsertText(item, "\n"));
	CHECK(item.Document->GetBuffer().Materialize() == "if xxxxxxxxxx:\n\n    body1\n    body2");

	// With the bug, resize() pads the new line onto the tail instead of at line 1,
	// and "    body2" — below the widened repair range — is left holding that pad
	// (0) instead of its own depth (4).
	CHECK(item.IndentDepths == std::vector<std::size_t>{0, 0, 4, 4});
	CHECK(item.WidestLine == 0);
	CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("A line-joining backspace keeps the indent depth of content below it")
{
	// Mirror of the insertion case, on the erase side: previousCount > currentCount
	// used to truncate the tail rather than remove the entry at the edit site, which
	// drops the depth of every surviving line below it — here, "two" loses its depth
	// of 8 and inherits "one"'s stale 4 instead.
	Core::OpenItem item = ItemWith("if xxxxxxxxxx:\n    one\n\n        two");
	REQUIRE(item.WidestLine == 0);
	REQUIRE(item.IndentDepths == std::vector<std::size_t>{0, 4, 4, 8});

	// Caret at the start of the blank line — Backspace joins it into "    one".
	item.Caret.Offset = 23;
	item.Caret.Anchor = 23;

	CHECK(Core::DeleteBackward(item));
	CHECK(item.Document->GetBuffer().Materialize() == "if xxxxxxxxxx:\n    one\n        two");

	CHECK(item.IndentDepths == std::vector<std::size_t>{0, 4, 8});
	CHECK(item.WidestLine == 0);
	CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("Undo through Editing restores the caret and the caches")
{
	Core::OpenItem item = ItemWith("short\nlonger line here");
	item.Caret.Offset = 22;
	item.Caret.Anchor = 22;

	CHECK(Core::InsertText(item, " and more"));
	const std::size_t widened = item.WidestColumn;

	CHECK(Core::Undo(item));
	CHECK(item.Document->GetBuffer().Materialize() == "short\nlonger line here");
	CHECK(item.WidestColumn < widened);
	CheckDerivedCachesAreCorrect(item);

	CHECK(Core::Redo(item));
	CHECK(item.WidestColumn == widened);
	CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("Enter carries the previous line's indentation")
{
	Core::OpenItem item = ItemWith("    if (x)");
	item.Caret.Offset = 10;
	item.Caret.Anchor = 10;

	CHECK(Core::InsertNewline(item, {}));
	CHECK(item.Document->GetBuffer().Materialize() == "    if (x)\n    ");
	CHECK(item.Caret.Offset == 15);
	CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("Enter in the middle of a line splits it and still indents")
{
	Core::OpenItem item = ItemWith("  ab");
	item.Caret.Offset = 3;
	item.Caret.Anchor = 3;

	CHECK(Core::InsertNewline(item, {}));
	CHECK(item.Document->GetBuffer().Materialize() == "  a\n  b");
	CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("Enter on a blank line adds no indentation of its own")
{
	Core::OpenItem item = ItemWith("");

	CHECK(Core::InsertNewline(item, {}));
	CHECK(item.Document->GetBuffer().Materialize() == "\n");
	CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("Tab without a selection inserts one indent")
{
	Core::OpenItem spaces = ItemWith("ab");
	spaces.Caret.Offset = 0;
	spaces.Caret.Anchor = 0;

	CHECK(Core::Indent(spaces, {}));
	CHECK(spaces.Document->GetBuffer().Materialize() == "    ab");
	CheckDerivedCachesAreCorrect(spaces);

	Core::OpenItem tabs = ItemWith("ab");
	CHECK(Core::Indent(tabs, Core::IndentStyle{false, 4}));
	CHECK(tabs.Document->GetBuffer().Materialize() == "\tab");
	CheckDerivedCachesAreCorrect(tabs);
}

TEST_CASE("Tab indents every line the selection touches, as one undo step")
{
	Core::OpenItem item = ItemWith("one\ntwo\nthree");
	item.Caret.Anchor = 1;
	// Offset 6 sits at the end of "two", still inside line 1 — the selection never
	// reaches line 2 at all. (The brief's original offset of 9 lands one byte *into*
	// "three", which by BlockLines' own rule means the selection does touch line 2 and
	// should indent it; the brief's expected string left "three" untouched, contradicting
	// its own algorithm. Moved the offset back onto line 1 so the test exercises what its
	// name says — a selection that stays within the lines it visibly highlights.)
	item.Caret.Offset = 6;

	CHECK(Core::Indent(item, {}));
	CHECK(item.Document->GetBuffer().Materialize() == "    one\n    two\nthree");
	CheckDerivedCachesAreCorrect(item);

	CHECK(item.Document->GetBuffer().Undo());
	CHECK(item.Document->GetBuffer().Materialize() == "one\ntwo\nthree");
}

TEST_CASE("A selection ending at the start of a line does not indent that line")
{
	Core::OpenItem item = ItemWith("one\ntwo\nthree");
	item.Caret.Anchor = 0;
	item.Caret.Offset = 8;

	CHECK(Core::Indent(item, {}));
	CHECK(item.Document->GetBuffer().Materialize() == "    one\n    two\nthree");
	CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("Shift+Tab removes one indent and never more than exists")
{
	Core::OpenItem item = ItemWith("        deep\n  shallow\nnone");
	item.Caret.Anchor = 0;
	item.Caret.Offset = item.Document->GetBuffer().GetLength();

	CHECK(Core::Outdent(item, {}));
	CHECK(item.Document->GetBuffer().Materialize() == "    deep\nshallow\nnone");
	CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("Shift+Tab with nothing to remove changes nothing")
{
	Core::OpenItem item = ItemWith("none");
	CHECK_FALSE(Core::Outdent(item, {}));
	CHECK(item.Document->GetBuffer().Materialize() == "none");
}

TEST_CASE("Shift+Tab with no selection removes the indent of just the caret's line")
{
	Core::OpenItem item = ItemWith("    ab");
	// Default caret: offset 0, anchor 0 — no selection, so BlockLines collapses to the
	// single line the caret sits on.
	CHECK(Core::Outdent(item, {}));
	CHECK(item.Document->GetBuffer().Materialize() == "ab");
	CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("Tab with no selection is its own undo step, as the block form is")
{
	Core::OpenItem item = ItemWith("");

	CHECK(Core::InsertText(item, "ab"));
	CHECK(Core::Indent(item, {}));
	CHECK(item.Document->GetBuffer().Materialize() == "ab    ");

	// Left to coalesce with the typing before it, one Ctrl+Z would take the "ab" with
	// it — and Shift+Tab, which has always broken the group, would answer the same
	// gesture differently.
	CHECK(Core::Undo(item));
	CHECK(item.Document->GetBuffer().Materialize() == "ab");
}

TEST_CASE("Shift+Tab with nothing to remove does not break the undo group")
{
	Core::OpenItem item = ItemWith("");

	CHECK(Core::InsertText(item, "ab"));
	CHECK_FALSE(Core::Outdent(item, {}));
	CHECK(Core::InsertText(item, "cd"));
	CHECK(item.Document->GetBuffer().Materialize() == "abcd");

	// One run of typing is one undo step. The refused Outdent made no edit, so it had no
	// business splitting it in two.
	CHECK(Core::Undo(item));
	CHECK(item.Document->GetBuffer().Materialize() == "");
}

TEST_CASE("A read-only document refuses newline and indentation")
{
	Core::OpenItem item = ItemWith("ab\x80xy");
	REQUIRE(item.Document->IsReadOnly());

	CHECK_FALSE(Core::InsertNewline(item, {}));
	CHECK_FALSE(Core::Indent(item, {}));
	CHECK_FALSE(Core::Outdent(item, {}));
}

// --- Coverage for UpdateDerived's incremental branch ---------------------------------
//
// Every cache assertion above edits at firstLine == 0, so item.WidestLine (unsigned,
// always >= 0) never satisfies `item.WidestLine < firstLine`, and UpdateDerived always
// takes the full RefreshDerived fallback. CheckDerivedCachesAreCorrect then compares one
// fresh recomputation against another — it cannot fail regardless of whether the
// incremental splice in UpdateIndentDepths is correct. The tests below put the widest
// line at line 0 and edit strictly below it, so the incremental branch is what actually
// runs, and each one says so explicitly before making the edit.

TEST_CASE("InsertNewline below the widest line splices indentDepths instead of "
		  "misaligning the tail")
{
	// Line 0 is the widest line and stays outside every edit below it.
	Core::OpenItem item = ItemWith("aaaaaaaaaa\nb\ncd");
	REQUIRE(item.WidestLine == 0);

	// Splits "cd" into "c" / "d" — line 2 is strictly below the widest line, so
	// UpdateDerived's `item.WidestLine < firstLine` check (0 < 2) takes the incremental
	// branch, and the line count grows from 3 to 4, exercising the splice in
	// UpdateIndentDepths rather than the resize bug it was written to prevent.
	item.Caret.Offset = 14;
	item.Caret.Anchor = 14;
	REQUIRE(item.Document->GetBuffer().ToPosition(item.Caret.Offset).Line == 2);

	CHECK(Core::InsertNewline(item, {}));
	CHECK(item.Document->GetBuffer().Materialize() == "aaaaaaaaaa\nb\nc\nd");
	CHECK(item.WidestLine == 0);
	CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("InsertNewline reaches past a run of blank lines to find the real anchor")
{
	// Line 0 is the widest line. Pressing Enter at the end of line 1 creates a new line
	// that is itself blank (just the copied indent), immediately above two further blank
	// lines and then a non-blank anchor at line 5. UpdateIndentDepths' `while (blank(to))
	// ++to;` has to walk through all three blank lines to reach that anchor — a
	// too-narrow repair range is exactly what this is built to catch.
	Core::OpenItem item = ItemWith("xxxxxxxxxxxxxxxx\n    if\n\n\n        end");
	REQUIRE(item.WidestLine == 0);

	item.Caret.Offset = 23; // end of "    if", the start of the run this test is about
	item.Caret.Anchor = 23;
	REQUIRE(item.Document->GetBuffer().ToPosition(item.Caret.Offset).Line == 1);

	CHECK(Core::InsertNewline(item, {}));
	CHECK(item.Document->GetBuffer().Materialize() == "xxxxxxxxxxxxxxxx\n    if\n    \n\n\n        end");
	CHECK(item.WidestLine == 0);
	CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("Indent on a block below the widest line takes the incremental path")
{
	Core::OpenItem item = ItemWith("xxxxxxxxxxxxxxxx\none\ntwo");
	REQUIRE(item.WidestLine == 0);

	// Selects lines 1 and 2, both strictly below the widest line.
	item.Caret.Anchor = 17;
	item.Caret.Offset = 24;
	REQUIRE(item.Document->GetBuffer().ToPosition(item.Caret.Anchor).Line == 1);

	CHECK(Core::Indent(item, {}));
	CHECK(item.Document->GetBuffer().Materialize() == "xxxxxxxxxxxxxxxx\n    one\n    two");
	CHECK(item.WidestLine == 0);
	CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("Outdent on a block below the widest line takes the incremental path")
{
	Core::OpenItem item = ItemWith("xxxxxxxxxxxxxxxx\n    one\n    two");
	REQUIRE(item.WidestLine == 0);

	item.Caret.Anchor = 17;
	item.Caret.Offset = 32;
	REQUIRE(item.Document->GetBuffer().ToPosition(item.Caret.Anchor).Line == 1);

	CHECK(Core::Outdent(item, {}));
	CHECK(item.Document->GetBuffer().Materialize() == "xxxxxxxxxxxxxxxx\none\ntwo");
	CHECK(item.WidestLine == 0);
	CheckDerivedCachesAreCorrect(item);
}

// --- The row map under editing --------------------------------------------------------
//
// The cases above all leave the map unwrapped, where a line is one row whatever happens to
// it and a misplaced splice cannot show. These wrap first, exactly as the widget does once
// it knows the width, so an entry landing against the wrong line changes a row count and
// is caught. Each edits strictly below the widest line, so UpdateDerived takes its
// incremental branch rather than falling back to a full RefreshDerived.

namespace {
	Core::OpenItem WrappedItemWith(std::string content, std::size_t columnsPerRow)
	{
		Core::OpenItem item = ItemWith(std::move(content));

		item.Rows.Rebuild(item.Document->GetBuffer(), columnsPerRow, Core::DefaultTabWidth);

		return item;
	}
}

namespace {
	// Line 0 is the widest and is never edited, so widestLine (0) < firstLine holds and
	// the incremental branch runs. At ten columns the three lines take three, two and one
	// row — all different on purpose. Row counts that happened to agree would let a splice
	// landing at the wrong line pass unnoticed, which is exactly what an earlier draft of
	// these cases did.
	const std::string kWrapFixture = "aaaaaaaaaaaaaaaaaaaaaaaaa\nbbbb cccc dddd eeee\nff";
}

TEST_CASE("InsertNewline below the widest line splices the row map into line")
{
	Core::OpenItem item = WrappedItemWith(kWrapFixture, 10);
	REQUIRE(item.WidestLine == 0);
	REQUIRE(item.Rows.GetLineCount() == 3);
	REQUIRE(item.Rows.RowsOf(0) == 3);
	REQUIRE(item.Rows.RowsOf(1) == 2);
	REQUIRE(item.Rows.RowsOf(2) == 1);

	const std::size_t rowsBefore = item.Rows.TotalRows();

	item.Caret.Offset = 47; // between the two f's on the last line
	item.Caret.Anchor = 47;
	REQUIRE(item.Document->GetBuffer().ToPosition(item.Caret.Offset).Line == 2);

	CHECK(Core::InsertNewline(item, {}));
	CHECK(item.Document->GetBuffer().Materialize() == "aaaaaaaaaaaaaaaaaaaaaaaaa\nbbbb cccc dddd eeee\nf\nf");

	// The width survives the edit: a rebuild at kNoWrap here would unwrap the document
	// under the user, and nothing else would notice.
	CHECK(item.Rows.GetColumnsPerRow() == 10);
	CHECK(item.Rows.GetLineCount() == item.Document->GetBuffer().GetLineCount());
	CHECK(item.Rows.TotalRows() == rowsBefore + 1);
	CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("Deleting a line break below the widest line splices the row map out again")
{
	Core::OpenItem item = WrappedItemWith(kWrapFixture, 10);
	REQUIRE(item.WidestLine == 0);
	REQUIRE(item.Rows.GetLineCount() == 3);

	// At the very start of the last line, so Backspace joins it to the one above. The
	// merged line is 21 columns and takes three rows where its two halves took three
	// between them, so the total alone would not prove the splice landed correctly — the
	// per-line comparison in CheckDerivedCachesAreCorrect is what does.
	item.Caret.Offset = 46;
	item.Caret.Anchor = 46;
	REQUIRE(item.Document->GetBuffer().ToPosition(item.Caret.Offset).Line == 2);

	CHECK(Core::DeleteBackward(item));
	CHECK(item.Document->GetBuffer().Materialize() == "aaaaaaaaaaaaaaaaaaaaaaaaa\nbbbb cccc dddd eeeeff");
	CHECK(item.Rows.GetColumnsPerRow() == 10);
	CHECK(item.Rows.GetLineCount() == 2);
	CHECK(item.Rows.RowsOf(0) == 3);
	CHECK(item.Rows.RowsOf(1) == 3);
	CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("Undo keeps the map wrapping to the width it was built for")
{
	// The regression test for the first defect found in this task: RefreshDerived rebuilt
	// at kNoWrap, so an undo silently unwrapped a document that was on screen — and since
	// the widget only rebuilds when the *width* changes, it stayed unwrapped until the
	// window was resized.
	Core::OpenItem item = WrappedItemWith(kWrapFixture, 10);
	const std::size_t rowsBefore = item.Rows.TotalRows();

	item.Caret.Offset = 48; // the end of the document
	item.Caret.Anchor = 48;
	CHECK(Core::InsertText(item, " gggg hhhh"));
	CHECK(item.Rows.TotalRows() == rowsBefore + 1);

	CHECK(Core::Undo(item));
	CHECK(item.Document->GetBuffer().Materialize() == kWrapFixture);
	CHECK(item.Rows.GetColumnsPerRow() == 10);
	CHECK(item.Rows.TotalRows() == rowsBefore);
	CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("Undo puts the caret where the change it reversed was")
{
	// The caret used to be left where it happened to be and merely clamped to the new
	// length, so undoing an insertion that sat before it shifted the text out from under
	// it: the offset stayed put while the bytes moved, and the caret ended up somewhere the
	// user never pointed at.
	Core::OpenItem item = ItemWith("abc");

	Core::InsertText(item, "XYZ");
	REQUIRE(item.Document->GetBuffer().Materialize() == "XYZabc");

	item.Caret.Offset = 6;
	item.Caret.Anchor = 6;

	REQUIRE(Core::Undo(item));

	CHECK(item.Document->GetBuffer().Materialize() == "abc");
	CHECK(item.Caret.Offset == 0);
	CHECK(item.Caret.Anchor == 0);
}

TEST_CASE("Redo puts the caret after the change it reapplied")
{
	Core::OpenItem item = ItemWith("abc");

	Core::InsertText(item, "XYZ");
	REQUIRE(Core::Undo(item));

	item.Caret.Offset = 3;
	item.Caret.Anchor = 3;

	REQUIRE(Core::Redo(item));

	CHECK(item.Document->GetBuffer().Materialize() == "XYZabc");
	CHECK(item.Caret.Offset == 3);
}
