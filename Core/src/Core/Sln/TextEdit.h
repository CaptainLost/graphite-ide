#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace core::sln::edit
{
    /// Helpers for editing a solution file as text.
    ///
    /// Solution files are edited rather than regenerated, because both formats carry
    /// sections this build does not model and a regenerated file would drop them. These
    /// find the boundaries a splice needs; they do not parse.

    /// The line ending the file already uses. Falls back to "\n" for a file with none.
    [[nodiscard]] std::string DetectLineEnding(std::string_view text);

    /// The indentation of the first indented line, which is the unit to indent new lines
    /// by. Falls back to two spaces.
    [[nodiscard]] std::string IndentUnit(std::string_view text);

    /// One past the '>' that ends the opening tag beginning at `offset`, where `offset`
    /// is pugixml's — one past the '<'. Quoted attribute values are skipped, so a '>'
    /// inside one does not end the tag early.
    [[nodiscard]] std::size_t OpeningTagEnd(std::string_view text, std::size_t offset);

    /// One past the end of the whole element beginning at `offset`, self-closing or not.
    /// `name` is the element name, needed to find the matching closing tag.
    [[nodiscard]] std::size_t ElementEnd(std::string_view text, std::size_t offset,
                                         std::string_view name);

    /// The half-open range of the line containing `position`, including its line ending.
    /// Used to remove a line whole rather than leaving a blank one behind.
    [[nodiscard]] std::pair<std::size_t, std::size_t> LineExtent(std::string_view text,
                                                                 std::size_t position);
}
