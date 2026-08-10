#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace core::text
{
    /// The columns at which `line` breaks when rows are `columnsPerRow` wide, ascending,
    /// empty when the line fits in one row. A break column is where the *next* row
    /// starts — the character at that column is the first one on it.
    ///
    /// Columns rather than bytes because the caller already thinks in the same grid the
    /// caret does; ByteOfColumn converts when the text has to be sliced.
    ///
    /// Breaks after the last space at or before the limit, so a space stays on the row it
    /// ends and no row ever begins with one. A run with no space in it — one long word, a
    /// URL — breaks hard at the limit instead of overflowing.
    ///
    /// `columnsPerRow` is the width left for text, with the gutter already subtracted:
    /// this function knows nothing about gutters. Zero and one are accepted rather than
    /// rejected — a window dragged narrower than a cell is a layout state, not an error —
    /// and both are treated as one column so the walk always advances.
    [[nodiscard]] std::vector<std::size_t> WrapColumns(std::string_view line,
                                                       std::size_t columnsPerRow,
                                                       std::size_t tabWidth);

    class TextBuffer;
    class RowMap;

    /// Where a position in the file sits on screen: an absolute row, as the clipper counts
    /// them, and a column within that row measured from its left edge.
    struct VisualPosition
    {
        std::size_t row = 0;
        std::size_t column = 0;
    };

    /// The screen position of a byte offset.
    ///
    /// An offset exactly at a break belongs to the **following** row. Without that rule
    /// the caret after End and the caret after Home on the next row are the same offset in
    /// the file but two different places on screen, and the cursor jumps depending on how
    /// it got there.
    [[nodiscard]] VisualPosition CaretToVisual(const TextBuffer& buffer, const RowMap& rows,
                                                std::size_t offset, std::size_t tabWidth);

    /// The inverse, clamping rather than failing: a column past the end of a row gives the
    /// last offset that still belongs to that row — one column short of where the next
    /// row starts, never that column itself, because CaretToVisual's own rule reads that
    /// column as the next row's first cell. Clamping there instead would round-trip to
    /// the row below, not the one clicked.
    ///
    /// Visibly: on a row broken at a space the caret lands just before that space; on a
    /// row broken hard mid-word it lands one cell short of the row's right edge. Both look
    /// like an off-by-one until this rule is remembered — they are the same rule as
    /// CaretToVisual's, read backwards.
    [[nodiscard]] std::size_t VisualToCaret(const TextBuffer& buffer, const RowMap& rows,
                                             VisualPosition position, std::size_t tabWidth);
}
