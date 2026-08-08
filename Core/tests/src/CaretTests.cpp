#include <doctest/doctest.h>

#include "Core/Text/Caret.h"
#include "Core/Text/Columns.h"
#include "Core/Text/TextBuffer.h"

#include <ostream>
#include <string>

using namespace core::text;

namespace
{
    // A caret that has landed at `offset` without ever having aimed at a column — the
    // state a click or a fresh document leaves it in, before any vertical motion resolves
    // a real desiredColumn from it.
    Caret At(std::size_t offset)
    {
        return Caret{offset, offset, NoDesiredColumn};
    }

    Caret Step(const TextBuffer& buffer, Caret caret, Motion motion, bool extend = false)
    {
        return Move(buffer, caret, motion, extend, 4, 10);
    }
}

TEST_CASE("A fresh caret has no selection")
{
    const Caret caret;

    CHECK_FALSE(HasSelection(caret));
    CHECK(SelectionRange(caret) == std::pair<std::size_t, std::size_t>{0, 0});
}

TEST_CASE("A selection reads the same whichever way it was dragged")
{
    const Caret forward{7, 2, 0};
    const Caret backward{2, 7, 0};

    CHECK(HasSelection(forward));
    CHECK(HasSelection(backward));
    CHECK(SelectionRange(forward) == SelectionRange(backward));
    CHECK(SelectionRange(forward) == std::pair<std::size_t, std::size_t>{2, 7});
}

TEST_CASE("Left and right move by a character, not a byte")
{
    const TextBuffer buffer{"a\xC4\x85\xF0\x9F\x99\x82""z"};   // a ą 🙂 z

    Caret caret = At(0);
    caret = Step(buffer, caret, Motion::Right);
    CHECK(caret.offset == 1);
    caret = Step(buffer, caret, Motion::Right);
    CHECK(caret.offset == 3);
    caret = Step(buffer, caret, Motion::Right);
    CHECK(caret.offset == 7);

    caret = Step(buffer, caret, Motion::Left);
    CHECK(caret.offset == 3);
    caret = Step(buffer, caret, Motion::Left);
    CHECK(caret.offset == 1);
}

TEST_CASE("Movement stops at the ends of the document")
{
    const TextBuffer buffer{"ab"};

    CHECK(Step(buffer, At(0), Motion::Left).offset == 0);
    CHECK(Step(buffer, At(2), Motion::Right).offset == 2);
}

TEST_CASE("Left and right cross line boundaries")
{
    const TextBuffer buffer{"ab\ncd"};

    CHECK(Step(buffer, At(2), Motion::Right).offset == 3);
    CHECK(Step(buffer, At(3), Motion::Left).offset == 2);
}

TEST_CASE("Home and End go to the ends of the line, not the document")
{
    const TextBuffer buffer{"first\nsecond\nthird"};

    CHECK(Step(buffer, At(9), Motion::LineStart).offset == 6);
    CHECK(Step(buffer, At(9), Motion::LineEnd).offset == 12);
}

TEST_CASE("Ctrl+Home and Ctrl+End go to the ends of the document")
{
    const TextBuffer buffer{"first\nsecond"};

    CHECK(Step(buffer, At(8), Motion::DocumentStart).offset == 0);
    CHECK(Step(buffer, At(2), Motion::DocumentEnd).offset == buffer.Length());
}

TEST_CASE("Down keeps the column across a shorter line")
{
    // Column 5 on line 0, then a two-column line, then a long line again. The caret has
    // to come back to column 5 rather than sticking to the short line's end.
    const TextBuffer buffer{"aaaaaaaa\nbb\ncccccccc"};

    Caret caret = At(5);
    caret = Step(buffer, caret, Motion::Down);
    CHECK(caret.offset == 11);            // end of "bb"

    caret = Step(buffer, caret, Motion::Down);
    CHECK(caret.offset == 12 + 5);        // column 5 of "cccccccc"
}

TEST_CASE("A stored column smaller than the actual one still wins")
{
    // The caret sits at offset 5 on line 0 — actual column 5 — but was built directly with
    // a stored desiredColumn of 1. That is a real, already-set aim (not the sentinel), so
    // Down must travel to column 1, not to the caret's current actual column of 5: line 1
    // is "bb" at offset 9, byte 1 of it (column 1) is offset 10.
    const TextBuffer buffer{"aaaaaaaa\nbb\ncccccccc"};

    const Caret caret{5, 5, 1};
    CHECK(Step(buffer, caret, Motion::Down).offset == 10);
}

TEST_CASE("A horizontal move resets the column the caret aims for")
{
    const TextBuffer buffer{"aaaaaaaa\nbb\ncccccccc"};

    Caret caret = At(5);
    caret = Step(buffer, caret, Motion::Down);
    caret = Step(buffer, caret, Motion::Left);    // now deliberately at column 1
    caret = Step(buffer, caret, Motion::Down);

    CHECK(caret.offset == 12 + 1);
}

TEST_CASE("Up and down respect tab stops rather than byte counts")
{
    // Line 0 is a tab then 'x' — 'x' is column 4. Line 1 is four spaces then 'y', so
    // column 4 is 'y' at byte 4 of that line.
    const TextBuffer buffer{"\tx\n    y"};

    Caret caret = At(1);                  // on 'x', column 4
    caret = Step(buffer, caret, Motion::Down);

    CHECK(caret.offset == 3 + 4);
}

TEST_CASE("Up on the first line and down on the last stay put")
{
    const TextBuffer buffer{"one\ntwo"};

    CHECK(Step(buffer, At(1), Motion::Up).offset == 1);
    CHECK(Step(buffer, At(5), Motion::Down).offset == 5);
}

TEST_CASE("Page movement covers a screen and clamps at the ends")
{
    std::string content;

    for (std::size_t line = 0; line < 50; ++line)
        content += "line\n";

    const TextBuffer buffer{content};

    const Caret start = At(buffer.LineStart(20));
    CHECK(Move(buffer, start, Motion::PageDown, false, 4, 10).offset == buffer.LineStart(30));
    CHECK(Move(buffer, start, Motion::PageUp, false, 4, 10).offset == buffer.LineStart(10));

    CHECK(Move(buffer, At(0), Motion::PageUp, false, 4, 10).offset == 0);

    const Caret last = At(buffer.LineStart(buffer.LineCount() - 1));
    CHECK(Move(buffer, last, Motion::PageDown, false, 4, 10).offset == last.offset);
}

TEST_CASE("Word movement crosses runs of like characters")
{
    const TextBuffer buffer{"int value_2 = other;"};

    CHECK(Step(buffer, At(0), Motion::WordRight).offset == 3);    // end of "int"
    CHECK(Step(buffer, At(3), Motion::WordRight).offset == 4);    // over the space
    CHECK(Step(buffer, At(4), Motion::WordRight).offset == 11);   // end of "value_2"

    CHECK(Step(buffer, At(11), Motion::WordLeft).offset == 4);
    CHECK(Step(buffer, At(4), Motion::WordLeft).offset == 3);
}

TEST_CASE("Word movement stops at the ends of the document")
{
    const TextBuffer buffer{"word"};

    CHECK(Step(buffer, At(0), Motion::WordLeft).offset == 0);
    CHECK(Step(buffer, At(4), Motion::WordRight).offset == 4);
}

TEST_CASE("Shift keeps the anchor, so movement extends the selection")
{
    const TextBuffer buffer{"abcdef"};

    Caret caret = At(2);
    caret = Step(buffer, caret, Motion::Right, true);
    caret = Step(buffer, caret, Motion::Right, true);

    CHECK(caret.anchor == 2);
    CHECK(caret.offset == 4);
    CHECK(SelectionRange(caret) == std::pair<std::size_t, std::size_t>{2, 4});
}

TEST_CASE("Movement without Shift collapses the selection")
{
    const TextBuffer buffer{"abcdef"};

    const Caret selected{4, 2, 0};
    const Caret moved = Step(buffer, selected, Motion::Right);

    CHECK_FALSE(HasSelection(moved));
    CHECK(moved.anchor == moved.offset);
}

TEST_CASE("A click lands on a character boundary and never inside one")
{
    const TextBuffer buffer{"a\xC4\x85""b"};

    // Byte 2 is the middle of 'ą'.
    CHECK(PlaceAt(buffer, Caret{}, 2, false, 4).offset == 1);
    CHECK(PlaceAt(buffer, Caret{}, 99, false, 4).offset == buffer.Length());
}

TEST_CASE("A click with Shift extends from the existing anchor")
{
    const TextBuffer buffer{"abcdef"};

    const Caret placed = PlaceAt(buffer, Caret{2, 2, 0}, 5, true, 4);

    CHECK(placed.anchor == 2);
    CHECK(placed.offset == 5);
}

TEST_CASE("Double click selects the word under the caret")
{
    const TextBuffer buffer{"int value_2 = other;"};

    const Caret word = SelectWordAt(buffer, 6);

    CHECK(SelectionRange(word) == std::pair<std::size_t, std::size_t>{4, 11});
}

TEST_CASE("Double click on whitespace selects the run of whitespace")
{
    const TextBuffer buffer{"a    b"};

    CHECK(SelectWordAt(buffer, 2).offset == 5);
    CHECK(SelectionRange(SelectWordAt(buffer, 2)) == std::pair<std::size_t, std::size_t>{1, 5});
}

TEST_CASE("Non-ASCII bytes count as letters, so a Polish word is one word")
{
    const TextBuffer buffer{"cz\xC4\x99\xC5\x9B\xC4\x87 next"};

    CHECK(SelectionRange(SelectWordAt(buffer, 0)).second == 8);
}

TEST_CASE("Triple click selects the line with its ending")
{
    const TextBuffer buffer{"one\ntwo\nthree"};

    CHECK(SelectionRange(SelectLineAt(buffer, 1)) == std::pair<std::size_t, std::size_t>{4, 8});
}

TEST_CASE("The last line has no ending to include")
{
    const TextBuffer buffer{"one\ntwo"};

    CHECK(SelectionRange(SelectLineAt(buffer, 1))
          == std::pair<std::size_t, std::size_t>{4, buffer.Length()});
}

TEST_CASE("Select all covers the document")
{
    const TextBuffer buffer{"one\ntwo"};

    const Caret all = SelectAll(buffer);

    CHECK(SelectionRange(all) == std::pair<std::size_t, std::size_t>{0, buffer.Length()});
}

TEST_CASE("Every motion leaves the caret on a character boundary")
{
    const TextBuffer buffer{"\t\xC4\x85\xF0\x9F\x99\x82 word\n\xC4\x99nd\nx"};

    const Motion motions[] = {Motion::Left,      Motion::Right,        Motion::Up,
                              Motion::Down,      Motion::LineStart,    Motion::LineEnd,
                              Motion::WordLeft,  Motion::WordRight,    Motion::PageUp,
                              Motion::PageDown,  Motion::DocumentStart, Motion::DocumentEnd};

    for (const Motion motion : motions)
    {
        for (std::size_t start = 0; start <= buffer.Length(); ++start)
        {
            const Caret result = Move(buffer, At(start), motion, false, 4, 3);

            REQUIRE(result.offset <= buffer.Length());

            // A boundary is any offset that is not a UTF-8 continuation byte.
            if (result.offset < buffer.Length())
                REQUIRE((static_cast<unsigned char>(buffer.CharAt(result.offset)) & 0xC0) != 0x80);
        }
    }
}
