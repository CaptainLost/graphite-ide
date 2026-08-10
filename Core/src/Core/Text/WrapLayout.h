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
}
