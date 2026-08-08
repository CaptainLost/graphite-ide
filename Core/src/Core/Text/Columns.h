#pragma once

#include <cstddef>
#include <string_view>

namespace core::text
{
    class TextBuffer;

    /// Fixed for now; it joins the editor font size in settings once there is a settings
    /// window to put it in.
    inline constexpr std::size_t DefaultTabWidth = 4;

    /// Screen column of a byte offset within one line's text.
    ///
    /// A column is a grid cell, not a byte: a character outside ASCII occupies one cell
    /// whatever its byte length, and a tab advances to the next multiple of `tabWidth`.
    /// With a monospaced font that makes all geometry arithmetic — x = column * cellWidth.
    ///
    /// `line` is a line's text without its ending, as TextBuffer::LineText returns it.
    /// A byte past the end of the line gives the column one past its last.
    [[nodiscard]] std::size_t ColumnOfByte(std::string_view line, std::size_t byte,
                                           std::size_t tabWidth);

    /// The inverse, rounding to the nearest boundary a caret may occupy. A column inside
    /// the run of a tab gives the byte of that tab; a column past the end of the line
    /// gives the line's length.
    [[nodiscard]] std::size_t ByteOfColumn(std::string_view line, std::size_t column,
                                           std::size_t tabWidth);

    /// Columns the line occupies — the column one past its last character.
    [[nodiscard]] std::size_t ColumnCount(std::string_view line, std::size_t tabWidth);

    /// Widest line in the document, in columns. This is the horizontal scrolling extent.
    /// Walks every line, so it is computed once when a document loads rather than per
    /// frame — nothing in this stage edits, so it never changes afterwards.
    [[nodiscard]] std::size_t WidestColumn(const TextBuffer& buffer, std::size_t tabWidth);
}
