#include "corepch.h"

#include "Core/Text/Indent.h"

#include "Core/Text/Columns.h"
#include "Core/Text/TextBuffer.h"

#include <algorithm>
#include <optional>

namespace core::text
{
    namespace
    {
        [[nodiscard]] bool IsIndentWhitespace(char c)
        {
            return c == ' ' || c == '\t';
        }

        [[nodiscard]] bool IsBlank(std::string_view line)
        {
            for (const char c : line)
            {
                if (!IsIndentWhitespace(c))
                    return false;
            }

            return true;
        }
    }

    std::size_t IndentDepth(std::string_view line, std::size_t tabWidth)
    {
        std::size_t byte = 0;

        while (byte < line.size() && IsIndentWhitespace(line[byte]))
            ++byte;

        if (byte == line.size())
            return 0;

        return ColumnOfByte(line, byte, tabWidth);
    }

    std::vector<std::size_t> IndentDepths(const TextBuffer& buffer, std::size_t tabWidth)
    {
        const std::size_t lineCount = buffer.LineCount();
        std::vector<std::size_t> depths(lineCount, 0);
        std::vector<bool> blank(lineCount, false);

        for (std::size_t line = 0; line < lineCount; ++line)
        {
            const std::string text = buffer.LineText(line);
            blank[line] = IsBlank(text);

            if (!blank[line])
                depths[line] = IndentDepth(text, tabWidth);
        }

        // Nearest non-blank depth at or before each line, carried forward.
        std::vector<std::optional<std::size_t>> above(lineCount);
        std::optional<std::size_t> lastNonBlank;

        for (std::size_t line = 0; line < lineCount; ++line)
        {
            if (!blank[line])
                lastNonBlank = depths[line];

            above[line] = lastNonBlank;
        }

        // Nearest non-blank depth at or after each line, carried backward.
        std::vector<std::optional<std::size_t>> below(lineCount);
        std::optional<std::size_t> nextNonBlank;

        for (std::size_t line = lineCount; line-- > 0;)
        {
            if (!blank[line])
                nextNonBlank = depths[line];

            below[line] = nextNonBlank;
        }

        for (std::size_t line = 0; line < lineCount; ++line)
        {
            if (!blank[line])
                continue;

            // A blank line belongs to neither the block above nor the block below, so a
            // guide is only honest at the shallower of the two — the smaller of the two
            // stops a guide from being drawn into a gap where the deeper block has
            // already closed. A missing side counts as depth 0 here, which is exactly
            // what "the depth is 0 with nothing on one side" means once folded into the
            // same minimum rather than handled as a separate case.
            depths[line] = std::min(above[line].value_or(0), below[line].value_or(0));
        }

        return depths;
    }
}
