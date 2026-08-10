#include "corepch.h"

#include "Core/Text/WrapLayout.h"

#include "Core/Text/Columns.h"
#include "Core/Text/RowMap.h"
#include "Core/Text/TextBuffer.h"

#include <algorithm>
#include <string>

namespace core::text
{
    std::vector<std::size_t> WrapColumns(std::string_view line, std::size_t columnsPerRow,
                                          std::size_t tabWidth)
    {
        std::vector<std::size_t> breaks;

        // A row narrower than one cell would make the walk below stand still. One column
        // is the narrowest width that still terminates, and a window that small is
        // unreadable either way.
        const std::size_t width = std::max<std::size_t>(columnsPerRow, 1);
        const std::size_t total = ColumnCount(line, tabWidth);

        std::size_t rowStart = 0;

        while (total - rowStart > width)
        {
            const std::size_t limit = rowStart + width;

            // The last space at or before the limit. Searched in columns, so a tab's
            // whole advance is skipped over rather than counted as one character.
            std::size_t breakAt = 0;

            for (std::size_t column = limit; column > rowStart; --column)
            {
                const std::size_t byte = ByteOfColumn(line, column - 1, tabWidth);

                if (byte < line.size() && line[byte] == ' ')
                {
                    breakAt = column;
                    break;
                }
            }

            if (breakAt > 0)
            {
                // Found a space; skip consecutive spaces so the next row starts on the
                // first non-space character. A run of spaces stays on the row it ends
                // even if that makes the row visually wider than columnsPerRow.
                while (breakAt < total)
                {
                    const std::size_t byte = ByteOfColumn(line, breakAt, tabWidth);

                    if (byte < line.size() && line[byte] == ' ')
                        breakAt++;
                    else
                        break;
                }
            }
            else
            {
                // No space to break on: a single word wider than the row. Breaking hard at
                // the limit keeps it on screen instead of letting it run off the edge.
                breakAt = limit;

                // If the hard break lands inside a tab's run, move back to the tab's start
                // so the whole tab goes to the next row. If the tab starts at the row's own
                // start, it is wider than the row, so break after it instead.
                if (breakAt > rowStart && breakAt <= total)
                {
                    const std::size_t byte = ByteOfColumn(line, breakAt - 1, tabWidth);

                    if (byte < line.size() && line[byte] == '\t')
                    {
                        const std::size_t tabStart = ColumnOfByte(line, byte, tabWidth);

                        if (tabStart == rowStart)
                        {
                            // Tab is wider than the row; break after it so the walk
                            // still advances.
                            breakAt = ColumnOfByte(line, byte + 1, tabWidth);
                        }
                        else
                        {
                            // Tab is narrower; move back to where it starts.
                            breakAt = tabStart;
                        }
                    }
                }
            }

            // If the adjusted break is at or past the end of the line, there is nothing
            // left to put on another row; stop without pushing a break.
            if (breakAt >= total)
                break;

            breaks.push_back(breakAt);
            rowStart = breakAt;
        }

        return breaks;
    }

    namespace
    {
        /// The line containing `offset`. TextBuffer indexes line starts but exposes no
        /// reverse lookup, and this is its only caller, so the search lives here rather
        /// than growing TextBuffer's interface for one use.
        [[nodiscard]] std::size_t LineOfOffset(const TextBuffer& buffer, std::size_t offset)
        {
            std::size_t low = 0;
            std::size_t high = buffer.LineCount() - 1;

            while (low < high)
            {
                const std::size_t middle = low + (high - low + 1) / 2;

                if (buffer.LineStart(middle) <= offset)
                    low = middle;
                else
                    high = middle - 1;
            }

            return low;
        }
    }

    VisualPosition CaretToVisual(const TextBuffer& buffer, const RowMap& rows,
                                  std::size_t offset, std::size_t tabWidth)
    {
        const std::size_t line = LineOfOffset(buffer, offset);
        const std::string text = buffer.LineText(line);
        const std::size_t column = ColumnOfByte(text, offset - buffer.LineStart(line), tabWidth);

        const std::vector<std::size_t>& breaks = rows.BreaksOf(line);

        // The last break at or before this column is the row the offset sits on. "At or
        // before" rather than "before" is the whole rule: a column equal to a break is the
        // first cell of the row that break starts.
        std::size_t subRow = 0;
        std::size_t rowStart = 0;

        for (std::size_t index = 0; index < breaks.size(); ++index)
        {
            if (breaks[index] > column)
                break;

            subRow = index + 1;
            rowStart = breaks[index];
        }

        return VisualPosition{rows.FirstRowOf(line) + subRow, column - rowStart};
    }

    std::size_t VisualToCaret(const TextBuffer& buffer, const RowMap& rows,
                               VisualPosition position, std::size_t tabWidth)
    {
        const RowRef ref = rows.RowAt(position.row);
        const std::string text = buffer.LineText(ref.line);

        const std::vector<std::size_t>& breaks = rows.BreaksOf(ref.line);

        const std::size_t rowStart = ref.subRow == 0 ? 0 : breaks[ref.subRow - 1];

        // Where this row ends: the column just before the next row's first cell (a break
        // is the following row's own first column, never this row's), or the end of the
        // line for the last row. Using the break itself here — rather than one less than
        // it — would let the clamp below land exactly on the break, which CaretToVisual
        // then reads back as the *next* row's start: the row-end clamp has to agree with
        // the row-start rule above, or the round trip breaks.
        const std::size_t rowEnd = ref.subRow < breaks.size() ? breaks[ref.subRow] - 1
                                                               : ColumnCount(text, tabWidth);

        const std::size_t column = std::min(rowStart + position.column, rowEnd);

        return buffer.LineStart(ref.line) + ByteOfColumn(text, column, tabWidth);
    }
}
