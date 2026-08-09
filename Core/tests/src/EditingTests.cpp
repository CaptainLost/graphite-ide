#include "Core/Text/Editing.h"

#include "Core/Text/Columns.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/Indent.h"
#include "Core/Text/TextDocument.h"

#include "TempDirectory.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    /// An OpenItem holding `content`, with the caret and both caches consistent — the
    /// state loading a file would leave behind.
    core::text::OpenItem ItemWith(std::string content)
    {
        core::text::OpenItem item;

        auto document = core::text::TextDocument::FromContent(std::move(content), "test.txt");
        REQUIRE(document);

        item.document = std::move(document).Value();
        core::text::RefreshDerived(item);

        return item;
    }

    /// Both caches recomputed from scratch, for comparison against the maintained ones.
    void CheckDerivedCachesAreCorrect(const core::text::OpenItem& item)
    {
        const core::text::TextBuffer& buffer = item.document->Buffer();

        CHECK(item.widestColumn
              == core::text::WidestColumn(buffer, core::text::DefaultTabWidth));
        CHECK(item.indentDepths
              == core::text::IndentDepths(buffer, core::text::DefaultTabWidth));
    }
}

TEST_CASE("Typing inserts at the caret")
{
    core::text::OpenItem item = ItemWith("ac");
    item.caret.offset = 1;
    item.caret.anchor = 1;

    CHECK(core::text::InsertText(item, "b"));
    CHECK(item.document->Buffer().Text() == "abc");
    CHECK(item.caret.offset == 2);
    CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("Typing replaces the selection")
{
    core::text::OpenItem item = ItemWith("one two three");
    item.caret.anchor = 4;
    item.caret.offset = 7;

    CHECK(core::text::InsertText(item, "X"));
    CHECK(item.document->Buffer().Text() == "one X three");
    CHECK(item.caret.offset == 5);
    CHECK(item.caret.anchor == 5);
    CHECK(item.document->Buffer().Undo());
    CHECK(item.document->Buffer().Text() == "one two three");
}

TEST_CASE("Pasted CRLF is normalised before it reaches the buffer")
{
    core::text::OpenItem item = ItemWith("");

    CHECK(core::text::InsertText(item, "a\r\nb\rc"));
    CHECK(item.document->Buffer().Text() == "a\nb\nc");
    CHECK(item.document->Buffer().LineCount() == 3);
    CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("Backspace removes the character before the caret")
{
    core::text::OpenItem item = ItemWith("abc");
    item.caret.offset = 2;
    item.caret.anchor = 2;

    CHECK(core::text::DeleteBackward(item));
    CHECK(item.document->Buffer().Text() == "ac");
    CHECK(item.caret.offset == 1);
    CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("Backspace removes a whole UTF-8 character")
{
    // "ąć" — two characters, four bytes. One backspace must take two of them.
    core::text::OpenItem item = ItemWith("\xc4\x85\xc4\x87");
    item.caret.offset = 4;
    item.caret.anchor = 4;

    CHECK(core::text::DeleteBackward(item));
    CHECK(item.document->Buffer().Text() == "\xc4\x85");
}

TEST_CASE("Backspace joins two lines and the caches follow")
{
    core::text::OpenItem item = ItemWith("    one\n    two");
    item.caret.offset = 8;
    item.caret.anchor = 8;

    CHECK(core::text::DeleteBackward(item));
    CHECK(item.document->Buffer().Text() == "    one    two");
    CHECK(item.document->Buffer().LineCount() == 1);
    CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("Backspace with a selection removes the selection")
{
    core::text::OpenItem item = ItemWith("one two three");
    item.caret.anchor = 4;
    item.caret.offset = 8;

    CHECK(core::text::DeleteBackward(item));
    CHECK(item.document->Buffer().Text() == "one three");
    CHECK(item.caret.offset == 4);
}

TEST_CASE("Delete removes the character after the caret")
{
    core::text::OpenItem item = ItemWith("abc");
    item.caret.offset = 1;
    item.caret.anchor = 1;

    CHECK(core::text::DeleteForward(item));
    CHECK(item.document->Buffer().Text() == "ac");
    CHECK(item.caret.offset == 1);
}

TEST_CASE("Word deletion works in both directions")
{
    core::text::OpenItem backward = ItemWith("one two three");
    backward.caret.offset = 7;
    backward.caret.anchor = 7;

    CHECK(core::text::DeleteWordBackward(backward));
    // A single WordLeft hop covers only the run of letters, not the space beyond it —
    // CaretTests documents that motion as one class-run at a time — so backspacing off
    // the end of "two" removes exactly "two" and leaves both its neighbouring spaces.
    CHECK(backward.document->Buffer().Text() == "one  three");

    core::text::OpenItem forward = ItemWith("one two three");
    forward.caret.offset = 4;
    forward.caret.anchor = 4;

    CHECK(core::text::DeleteWordForward(forward));
    CHECK(forward.document->Buffer().Text() == "one  three");
}

TEST_CASE("Deleting at the ends of the document changes nothing")
{
    core::text::OpenItem start = ItemWith("abc");
    CHECK_FALSE(core::text::DeleteBackward(start));
    CHECK(start.document->Buffer().Text() == "abc");

    core::text::OpenItem end = ItemWith("abc");
    end.caret.offset = 3;
    end.caret.anchor = 3;
    CHECK_FALSE(core::text::DeleteForward(end));
    CHECK(end.document->Buffer().Text() == "abc");
}

TEST_CASE("Deleting the widest line shrinks widestColumn back down")
{
    core::text::OpenItem item = ItemWith("short\nmuch much longer line\nshort");
    item.caret.anchor = 6;
    item.caret.offset = 28;

    CHECK(core::text::DeleteBackward(item));
    CheckDerivedCachesAreCorrect(item);
    CHECK(item.widestColumn == 5);
}

TEST_CASE("A read-only document refuses every edit")
{
    // Invalid UTF-8: a lone continuation byte. FromContent keeps the bytes and marks the
    // document read-only rather than refusing to open it.
    core::text::OpenItem item = ItemWith("ab\x80xy");
    REQUIRE(item.document->IsReadOnly());

    CHECK_FALSE(core::text::InsertText(item, "z"));
    CHECK_FALSE(core::text::DeleteBackward(item));
    CHECK_FALSE(core::text::DeleteForward(item));
    CHECK(item.document->Buffer().Text() == "ab\x80xy");
}

TEST_CASE("A reload that turns the document read-only can still be undone and redone")
{
    // The one class of reload where what is on screen genuinely changes character: a
    // clean UTF-8 file rewritten by another tool in some other encoding. The reload
    // happens without asking, and the design justifies that precisely by the reload
    // being undoable — so it has to be, even though the document is read-only afterwards.
    TempDirectory temp;
    const std::filesystem::path path = temp.MakeFile("sample.txt", "hello\n");

    auto loaded = core::text::TextDocument::Load(path);
    REQUIRE(loaded);

    core::text::OpenItem item;
    item.document = std::move(loaded).Value();
    core::text::RefreshDerived(item);
    REQUIRE_FALSE(item.document->IsReadOnly());

    {
        // Latin-1: 0x80 is a lone continuation byte, so the content is no longer valid
        // UTF-8 and reloading marks the document read-only.
        const std::string latin1 = "ab\x80xy\n";
        std::ofstream stream{path, std::ios::binary | std::ios::trunc};
        stream.write(latin1.data(), static_cast<std::streamsize>(latin1.size()));
    }

    REQUIRE(item.document->ReloadFromDisk());
    REQUIRE(item.document->IsReadOnly());
    REQUIRE(item.document->Buffer().Text() == "ab\x80xy\n");

    CHECK(core::text::Undo(item));
    CHECK(item.document->Buffer().Text() == "hello\n");
    CheckDerivedCachesAreCorrect(item);

    // And what comes back is writable bytes, not a mixture: undo restores a whole content
    // the buffer already held rather than inserting anything into the one on screen,
    // which is why the read-only flag has no business stopping it.
    CHECK(item.document->Bytes() == "hello\n");

    CHECK(core::text::Redo(item));
    CHECK(item.document->Buffer().Text() == "ab\x80xy\n");
    CHECK(item.document->Bytes() == "ab\x80xy\n");

    // Read-only still means read-only for everything that creates new content.
    CHECK_FALSE(core::text::InsertText(item, "z"));
    CHECK_FALSE(core::text::InsertNewline(item, {}));
}

TEST_CASE("A line inserted above unrelated content keeps that content's indent depth")
{
    // Regression test: indentDepths used to be patched with vector::resize, which pads
    // or truncates the *tail* rather than shifting entries at the edit site. Line 0 is
    // deliberately the widest line, so the edit below it takes the cheap incremental
    // path in UpdateDerived rather than a full RefreshDerived that would hide the bug.
    core::text::OpenItem item = ItemWith("if xxxxxxxxxx:\n    body1\n    body2");
    REQUIRE(item.widestLine == 0);
    REQUIRE(item.indentDepths == std::vector<std::size_t>{0, 4, 4});

    // Caret at the start of "    body1" — pressing Enter right above it.
    item.caret.offset = 15;
    item.caret.anchor = 15;

    CHECK(core::text::InsertText(item, "\n"));
    CHECK(item.document->Buffer().Text() == "if xxxxxxxxxx:\n\n    body1\n    body2");

    // With the bug, resize() pads the new line onto the tail instead of at line 1,
    // and "    body2" — below the widened repair range — is left holding that pad
    // (0) instead of its own depth (4).
    CHECK(item.indentDepths == std::vector<std::size_t>{0, 0, 4, 4});
    CHECK(item.widestLine == 0);
    CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("A line-joining backspace keeps the indent depth of content below it")
{
    // Mirror of the insertion case, on the erase side: previousCount > currentCount
    // used to truncate the tail rather than remove the entry at the edit site, which
    // drops the depth of every surviving line below it — here, "two" loses its depth
    // of 8 and inherits "one"'s stale 4 instead.
    core::text::OpenItem item = ItemWith("if xxxxxxxxxx:\n    one\n\n        two");
    REQUIRE(item.widestLine == 0);
    REQUIRE(item.indentDepths == std::vector<std::size_t>{0, 4, 4, 8});

    // Caret at the start of the blank line — Backspace joins it into "    one".
    item.caret.offset = 23;
    item.caret.anchor = 23;

    CHECK(core::text::DeleteBackward(item));
    CHECK(item.document->Buffer().Text() == "if xxxxxxxxxx:\n    one\n        two");

    CHECK(item.indentDepths == std::vector<std::size_t>{0, 4, 8});
    CHECK(item.widestLine == 0);
    CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("Undo through Editing restores the caret and the caches")
{
    core::text::OpenItem item = ItemWith("short\nlonger line here");
    item.caret.offset = 22;
    item.caret.anchor = 22;

    CHECK(core::text::InsertText(item, " and more"));
    const std::size_t widened = item.widestColumn;

    CHECK(core::text::Undo(item));
    CHECK(item.document->Buffer().Text() == "short\nlonger line here");
    CHECK(item.widestColumn < widened);
    CheckDerivedCachesAreCorrect(item);

    CHECK(core::text::Redo(item));
    CHECK(item.widestColumn == widened);
    CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("Enter carries the previous line's indentation")
{
    core::text::OpenItem item = ItemWith("    if (x)");
    item.caret.offset = 10;
    item.caret.anchor = 10;

    CHECK(core::text::InsertNewline(item, {}));
    CHECK(item.document->Buffer().Text() == "    if (x)\n    ");
    CHECK(item.caret.offset == 15);
    CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("Enter in the middle of a line splits it and still indents")
{
    core::text::OpenItem item = ItemWith("  ab");
    item.caret.offset = 3;
    item.caret.anchor = 3;

    CHECK(core::text::InsertNewline(item, {}));
    CHECK(item.document->Buffer().Text() == "  a\n  b");
    CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("Enter on a blank line adds no indentation of its own")
{
    core::text::OpenItem item = ItemWith("");

    CHECK(core::text::InsertNewline(item, {}));
    CHECK(item.document->Buffer().Text() == "\n");
    CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("Tab without a selection inserts one indent")
{
    core::text::OpenItem spaces = ItemWith("ab");
    spaces.caret.offset = 0;
    spaces.caret.anchor = 0;

    CHECK(core::text::Indent(spaces, {}));
    CHECK(spaces.document->Buffer().Text() == "    ab");
    CheckDerivedCachesAreCorrect(spaces);

    core::text::OpenItem tabs = ItemWith("ab");
    CHECK(core::text::Indent(tabs, core::text::IndentStyle{false, 4}));
    CHECK(tabs.document->Buffer().Text() == "\tab");
    CheckDerivedCachesAreCorrect(tabs);
}

TEST_CASE("Tab indents every line the selection touches, as one undo step")
{
    core::text::OpenItem item = ItemWith("one\ntwo\nthree");
    item.caret.anchor = 1;
    // Offset 6 sits at the end of "two", still inside line 1 — the selection never
    // reaches line 2 at all. (The brief's original offset of 9 lands one byte *into*
    // "three", which by BlockLines' own rule means the selection does touch line 2 and
    // should indent it; the brief's expected string left "three" untouched, contradicting
    // its own algorithm. Moved the offset back onto line 1 so the test exercises what its
    // name says — a selection that stays within the lines it visibly highlights.)
    item.caret.offset = 6;

    CHECK(core::text::Indent(item, {}));
    CHECK(item.document->Buffer().Text() == "    one\n    two\nthree");
    CheckDerivedCachesAreCorrect(item);

    CHECK(item.document->Buffer().Undo());
    CHECK(item.document->Buffer().Text() == "one\ntwo\nthree");
}

TEST_CASE("A selection ending at the start of a line does not indent that line")
{
    core::text::OpenItem item = ItemWith("one\ntwo\nthree");
    item.caret.anchor = 0;
    item.caret.offset = 8;

    CHECK(core::text::Indent(item, {}));
    CHECK(item.document->Buffer().Text() == "    one\n    two\nthree");
    CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("Shift+Tab removes one indent and never more than exists")
{
    core::text::OpenItem item = ItemWith("        deep\n  shallow\nnone");
    item.caret.anchor = 0;
    item.caret.offset = item.document->Buffer().Length();

    CHECK(core::text::Outdent(item, {}));
    CHECK(item.document->Buffer().Text() == "    deep\nshallow\nnone");
    CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("Shift+Tab with nothing to remove changes nothing")
{
    core::text::OpenItem item = ItemWith("none");
    CHECK_FALSE(core::text::Outdent(item, {}));
    CHECK(item.document->Buffer().Text() == "none");
}

TEST_CASE("Shift+Tab with no selection removes the indent of just the caret's line")
{
    core::text::OpenItem item = ItemWith("    ab");
    // Default caret: offset 0, anchor 0 — no selection, so BlockLines collapses to the
    // single line the caret sits on.
    CHECK(core::text::Outdent(item, {}));
    CHECK(item.document->Buffer().Text() == "ab");
    CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("A read-only document refuses newline and indentation")
{
    core::text::OpenItem item = ItemWith("ab\x80xy");
    REQUIRE(item.document->IsReadOnly());

    CHECK_FALSE(core::text::InsertNewline(item, {}));
    CHECK_FALSE(core::text::Indent(item, {}));
    CHECK_FALSE(core::text::Outdent(item, {}));
}

// --- Coverage for UpdateDerived's incremental branch ---------------------------------
//
// Every cache assertion above edits at firstLine == 0, so item.widestLine (unsigned,
// always >= 0) never satisfies `item.widestLine < firstLine`, and UpdateDerived always
// takes the full RefreshDerived fallback. CheckDerivedCachesAreCorrect then compares one
// fresh recomputation against another — it cannot fail regardless of whether the
// incremental splice in UpdateIndentDepths is correct. The tests below put the widest
// line at line 0 and edit strictly below it, so the incremental branch is what actually
// runs, and each one says so explicitly before making the edit.

TEST_CASE("InsertNewline below the widest line splices indentDepths instead of "
          "misaligning the tail")
{
    // Line 0 is the widest line and stays outside every edit below it.
    core::text::OpenItem item = ItemWith("aaaaaaaaaa\nb\ncd");
    REQUIRE(item.widestLine == 0);

    // Splits "cd" into "c" / "d" — line 2 is strictly below the widest line, so
    // UpdateDerived's `item.widestLine < firstLine` check (0 < 2) takes the incremental
    // branch, and the line count grows from 3 to 4, exercising the splice in
    // UpdateIndentDepths rather than the resize bug it was written to prevent.
    item.caret.offset = 14;
    item.caret.anchor = 14;
    REQUIRE(item.document->Buffer().ToPosition(item.caret.offset).line == 2);

    CHECK(core::text::InsertNewline(item, {}));
    CHECK(item.document->Buffer().Text() == "aaaaaaaaaa\nb\nc\nd");
    CHECK(item.widestLine == 0);
    CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("InsertNewline reaches past a run of blank lines to find the real anchor")
{
    // Line 0 is the widest line. Pressing Enter at the end of line 1 creates a new line
    // that is itself blank (just the copied indent), immediately above two further blank
    // lines and then a non-blank anchor at line 5. UpdateIndentDepths' `while (blank(to))
    // ++to;` has to walk through all three blank lines to reach that anchor — a
    // too-narrow repair range is exactly what this is built to catch.
    core::text::OpenItem item = ItemWith("xxxxxxxxxxxxxxxx\n    if\n\n\n        end");
    REQUIRE(item.widestLine == 0);

    item.caret.offset = 23; // end of "    if", the start of the run this test is about
    item.caret.anchor = 23;
    REQUIRE(item.document->Buffer().ToPosition(item.caret.offset).line == 1);

    CHECK(core::text::InsertNewline(item, {}));
    CHECK(item.document->Buffer().Text()
          == "xxxxxxxxxxxxxxxx\n    if\n    \n\n\n        end");
    CHECK(item.widestLine == 0);
    CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("Indent on a block below the widest line takes the incremental path")
{
    core::text::OpenItem item = ItemWith("xxxxxxxxxxxxxxxx\none\ntwo");
    REQUIRE(item.widestLine == 0);

    // Selects lines 1 and 2, both strictly below the widest line.
    item.caret.anchor = 17;
    item.caret.offset = 24;
    REQUIRE(item.document->Buffer().ToPosition(item.caret.anchor).line == 1);

    CHECK(core::text::Indent(item, {}));
    CHECK(item.document->Buffer().Text() == "xxxxxxxxxxxxxxxx\n    one\n    two");
    CHECK(item.widestLine == 0);
    CheckDerivedCachesAreCorrect(item);
}

TEST_CASE("Outdent on a block below the widest line takes the incremental path")
{
    core::text::OpenItem item = ItemWith("xxxxxxxxxxxxxxxx\n    one\n    two");
    REQUIRE(item.widestLine == 0);

    item.caret.anchor = 17;
    item.caret.offset = 32;
    REQUIRE(item.document->Buffer().ToPosition(item.caret.anchor).line == 1);

    CHECK(core::text::Outdent(item, {}));
    CHECK(item.document->Buffer().Text() == "xxxxxxxxxxxxxxxx\none\ntwo");
    CHECK(item.widestLine == 0);
    CheckDerivedCachesAreCorrect(item);
}
