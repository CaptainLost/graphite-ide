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

            // No space to break on: a single word wider than the row. Breaking hard at
            // the limit keeps it on screen instead of letting it run off the edge.
            if (breakAt == 0)
                breakAt = limit;

            breaks.push_back(breakAt);
            rowStart = breakAt;
        }

        return breaks;
    }
}
