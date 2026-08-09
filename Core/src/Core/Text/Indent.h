#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace core::text
{
    class TextBuffer;

    /// Screen column (see Columns.h) of the first non-whitespace character in `line`.
    ///
    /// A line that is entirely whitespace, or empty, has no first non-whitespace
    /// character and reports 0 here — resolving what a blank line's depth ought to be
    /// is IndentDepths' job, not this one's, because that answer depends on the lines
    /// around it and this function only ever sees one line at a time.
    [[nodiscard]] std::size_t IndentDepth(std::string_view line, std::size_t tabWidth);

    /// One indent depth per line of `buffer`, with blank lines resolved.
    ///
    /// A blank line (empty or whitespace-only) has no indentation of its own, but a
    /// guide has to run through it unbroken rather than stop and restart around it. Its
    /// depth is the smaller of the nearest non-blank depth above it and the nearest
    /// non-blank depth below it — the smaller, not an average or either one alone,
    /// because a blank line between a deeply indented block and a line back at column 0
    /// belongs to neither, and drawing a guide through it at the deeper depth would
    /// suggest structure that has already closed. With no non-blank line on one side,
    /// the depth is 0.
    ///
    /// Walks every line, so it is computed once when a document loads rather than per
    /// frame — see OpenItem::widestColumn in DocumentSet.h for the same pattern and the
    /// same caveat about what has to keep it current once editing exists.
    [[nodiscard]] std::vector<std::size_t> IndentDepths(const TextBuffer& buffer,
                                                         std::size_t tabWidth);
}
