#pragma once

#include <cstddef>
#include <string>
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

    /// The line with every tab replaced by the spaces that reach its next tab stop.
    ///
    /// A tab is an advance rather than a glyph, so a renderer drawing the raw bytes would
    /// put every character after the first tab in the wrong place. Expanding first keeps
    /// a line one draw call instead of one run per tab, and the result agrees with
    /// ColumnOfByte by construction — both walk the line the same way.
    ///
    /// Linear in the length of the line: the column is carried through the walk, not
    /// recomputed from byte 0 for each tab. A tab-separated export is an ordinary text
    /// file as far as the editor is concerned, and re-deriving the column per tab makes
    /// drawing one quadratic in the number of tabs on the line.
    [[nodiscard]] std::string ExpandTabs(std::string_view line, std::size_t tabWidth);

    /// Widest line in the document, in columns. This is the horizontal scrolling extent.
    /// Walks every line, so it is computed once when a document loads rather than per
    /// frame — nothing in this stage edits, so it never changes afterwards.
    [[nodiscard]] std::size_t WidestColumn(const TextBuffer& buffer, std::size_t tabWidth);
}
