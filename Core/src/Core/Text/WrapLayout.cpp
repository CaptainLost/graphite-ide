#include "corepch.h"

#include "Core/Text/WrapLayout.h"

#include "Core/Text/Columns.h"

#include <algorithm>

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

                // If advancing past spaces reached the end of the line, there is nothing
                // left to put on another row; stop without pushing a break.
                if (breakAt >= total)
                    break;
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
                            if (byte + 1 <= line.size())
                                breakAt = ColumnOfByte(line, byte + 1, tabWidth);
                            else
                                breakAt = ColumnCount(line, tabWidth);
                        }
                        else
                        {
                            // Tab is narrower; move back to where it starts.
                            breakAt = tabStart;
                        }
                    }
                }
            }

            breaks.push_back(breakAt);
            rowStart = breakAt;
        }

        return breaks;
    }
}
