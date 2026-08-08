#include "corepch.h"

#include "Core/Text/Columns.h"

#include "Core/Text/Encoding.h"
#include "Core/Text/TextBuffer.h"

#include <algorithm>

namespace core::text
{
    namespace
    {
        /// Where a tab starting at `column` lands.
        [[nodiscard]] std::size_t AfterTab(std::size_t column, std::size_t tabWidth)
        {
            const std::size_t width = tabWidth == 0 ? 1 : tabWidth;

            return column + width - column % width;
        }
    }

    std::size_t ColumnOfByte(std::string_view line, std::size_t byte, std::size_t tabWidth)
    {
        const std::size_t target = std::min(byte, line.size());

        std::size_t column = 0;
        std::size_t at = 0;

        while (at < target)
        {
            if (line[at] == '\t')
            {
                column = AfterTab(column, tabWidth);
                ++at;
                continue;
            }

            ++column;
            at = NextCharacter(line, at);
        }

        return column;
    }

    std::size_t ByteOfColumn(std::string_view line, std::size_t column, std::size_t tabWidth)
    {
        std::size_t at = 0;
        std::size_t current = 0;

        while (at < line.size())
        {
            // A tab covers a run of columns and a character has no inside, so any column
            // before the next boundary resolves to the byte that starts this character.
            const std::size_t next =
                line[at] == '\t' ? AfterTab(current, tabWidth) : current + 1;

            if (column < next)
                return at;

            current = next;
            at = line[at] == '\t' ? at + 1 : NextCharacter(line, at);
        }

        return line.size();
    }

    std::size_t ColumnCount(std::string_view line, std::size_t tabWidth)
    {
        return ColumnOfByte(line, line.size(), tabWidth);
    }

    std::size_t WidestColumn(const TextBuffer& buffer, std::size_t tabWidth)
    {
        std::size_t widest = 0;

        for (std::size_t line = 0; line < buffer.LineCount(); ++line)
            widest = std::max(widest, ColumnCount(buffer.LineText(line), tabWidth));

        return widest;
    }
}
