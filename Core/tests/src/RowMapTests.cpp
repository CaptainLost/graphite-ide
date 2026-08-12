#include <doctest/doctest.h>

#include "Core/Text/RowMap.h"
#include "Core/Text/TextBuffer.h"
#include "Core/Text/WrapLayout.h"

#include <string>

using namespace Core;

namespace
{
    TextBuffer BufferOf(const std::string& text)
    {
        return TextBuffer{text};
    }
}

TEST_CASE("Without wrapping every line is one row")
{
    const TextBuffer buffer = BufferOf("aaa\nbbb\nccc");

    RowMap rows;
    rows.Rebuild(buffer, 80, 4);

    CHECK(rows.TotalRows() == 3);
    CHECK(rows.FirstRowOf(0) == 0);
    CHECK(rows.FirstRowOf(2) == 2);
    CHECK(rows.RowAt(1).line == 1);
    CHECK(rows.RowAt(1).subRow == 0);
}

TEST_CASE("A wrapped line occupies as many rows as it has breaks plus one")
{
    const TextBuffer buffer = BufferOf("aaaaaaaaaa\nbb");

    RowMap rows;
    rows.Rebuild(buffer, 4, 4);

    // Ten columns at four per row: rows at columns 0, 4 and 8.
    CHECK(rows.RowsOf(0) == 3);
    CHECK(rows.RowsOf(1) == 1);
    CHECK(rows.TotalRows() == 4);
}

TEST_CASE("A row resolves to its line and its position within it")
{
    const TextBuffer buffer = BufferOf("aaaaaaaaaa\nbb");

    RowMap rows;
    rows.Rebuild(buffer, 4, 4);

    CHECK(rows.RowAt(0).line == 0);
    CHECK(rows.RowAt(0).subRow == 0);
    CHECK(rows.RowAt(2).line == 0);
    CHECK(rows.RowAt(2).subRow == 2);
    CHECK(rows.RowAt(3).line == 1);
    CHECK(rows.RowAt(3).subRow == 0);
}

TEST_CASE("FirstRowOf and RowAt agree in both directions")
{
    const TextBuffer buffer = BufferOf("aaaaaaaaaa\nbb\ncccccc\nd");

    RowMap rows;
    rows.Rebuild(buffer, 4, 4);

    for (std::size_t line = 0; line < buffer.LineCount(); ++line)
    {
        const std::size_t row = rows.FirstRowOf(line);

        CHECK(rows.RowAt(row).line == line);
        CHECK(rows.RowAt(row).subRow == 0);
    }
}

TEST_CASE("Updating one line moves every row after it")
{
    TextBuffer buffer = BufferOf("aa\nbb\ncc");

    RowMap rows;
    rows.Rebuild(buffer, 4, 4);
    CHECK(rows.TotalRows() == 3);
    CHECK(rows.FirstRowOf(2) == 2);

    // The middle line grows to three rows; the line after it must shift by two.
    // TextBuffer has Insert and Erase, no Replace.
    buffer.Erase(3, 5);
    buffer.Insert(3, "bbbbbbbbbb");
    rows.UpdateLine(buffer, 1);

    CHECK(rows.RowsOf(1) == 3);
    CHECK(rows.TotalRows() == 5);
    CHECK(rows.FirstRowOf(2) == 4);
}

TEST_CASE("kNoWrap gives every line exactly one row")
{
    // What the editor uses when wrapping is off, so the widget takes one path either way
    // instead of branching between a wrapped and an unwrapped renderer. It cannot be
    // expressed as a width: WrapColumns treats a width of zero as one column, which would
    // wrap everything rather than nothing.
    const TextBuffer buffer = BufferOf("aaaaaaaaaaaaaaaaaaaa\nbb");

    RowMap rows;
    rows.Rebuild(buffer, RowMap::kNoWrap, 4);

    CHECK(rows.TotalRows() == 2);
    CHECK(rows.BreaksOf(0).empty());
}

TEST_CASE("A line of zero rows is skipped, which is what folding will need")
{
    const TextBuffer buffer = BufferOf("aa\nbb\ncc");

    RowMap rows;
    rows.Rebuild(buffer, 80, 4);
    rows.SetRowsOf(1, 0);

    CHECK(rows.TotalRows() == 2);
    CHECK(rows.RowAt(0).line == 0);
    CHECK(rows.RowAt(1).line == 2);
}

TEST_CASE("Two adjacent zero-row lines are both skipped")
{
    // What a folded block of several lines looks like to this map. The descent has to
    // step over a run of them, not just a single one.
    const TextBuffer buffer = BufferOf("aa\nbb\ncc\ndd");

    RowMap rows;
    rows.Rebuild(buffer, 80, 4);
    rows.SetRowsOf(1, 0);
    rows.SetRowsOf(2, 0);

    CHECK(rows.TotalRows() == 2);
    CHECK(rows.RowAt(0).line == 0);
    CHECK(rows.RowAt(1).line == 3);
    CHECK(rows.FirstRowOf(3) == 1);
}

TEST_CASE("A zero-row first line does not claim row zero")
{
    // The descent starts at line zero, so a folded first line is the case most likely to
    // be resolved to the wrong line.
    const TextBuffer buffer = BufferOf("aa\nbb");

    RowMap rows;
    rows.Rebuild(buffer, 80, 4);
    rows.SetRowsOf(0, 0);

    CHECK(rows.TotalRows() == 1);
    CHECK(rows.RowAt(0).line == 1);
    CHECK(rows.RowAt(0).subRow == 0);
    CHECK(rows.FirstRowOf(1) == 0);
}

TEST_CASE("A caret on a visible line maps correctly around a zero-row line")
{
    // CaretToVisual assumes every line occupies at least one row, and a caret *inside* a
    // zero-row line has no defined answer until stage 5's folding decides what one means.
    // The lines either side of it are not in doubt, though: they are ordinary visible
    // lines whose rows have simply moved up. Pinned here so folding cannot quietly break
    // the composition of a zero-row map with this pair while the undefined case is still
    // being designed.
    const TextBuffer buffer = BufferOf("aa\nbb\ncc");

    RowMap rows;
    rows.Rebuild(buffer, 80, 4);
    rows.SetRowsOf(1, 0);

    // Line 0 keeps row 0; line 2 (offsets 6 and 7) moves up to row 1.
    const VisualPosition before = CaretToVisual(buffer, rows, 1, 4);

    CHECK(before.row == 0);
    CHECK(before.column == 1);

    const VisualPosition after = CaretToVisual(buffer, rows, 7, 4);

    CHECK(after.row == 1);
    CHECK(after.column == 1);

    CHECK(VisualToCaret(buffer, rows, before, 4) == 1);
    CHECK(VisualToCaret(buffer, rows, after, 4) == 7);
}
