#include "Core/Text/Editing.h"

#include "Core/Text/Columns.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/Indent.h"
#include "Core/Text/TextDocument.h"

#include <doctest/doctest.h>

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
