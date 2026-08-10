#include "corepch.h"

#include "Core/Text/RowMap.h"

#include "Core/Text/TextBuffer.h"
#include "Core/Text/WrapLayout.h"

#include <algorithm>

namespace core::text
{
    namespace
    {
        const std::vector<std::size_t> kNoBreaks;
    }

    void RowMap::Rebuild(const TextBuffer& buffer, std::size_t columnsPerRow,
                          std::size_t tabWidth)
    {
        m_columnsPerRow = columnsPerRow;
        m_tabWidth = tabWidth;

        const std::size_t lines = buffer.LineCount();

        m_rowsPerLine.assign(lines, 0);
        m_breaks.assign(lines, {});
        m_tree.assign(lines + 1, 0);
        m_totalRows = 0;

        // Built by repeated SetCount rather than a linear-time Fenwick construction: this
        // runs on width changes, not per frame, and one obviously correct path is worth
        // more here than the constant factor.
        for (std::size_t line = 0; line < lines; ++line)
        {
            if (columnsPerRow != kNoWrap)
                m_breaks[line] = WrapColumns(buffer.LineText(line), columnsPerRow, tabWidth);

            SetCount(line, m_breaks[line].size() + 1);
        }
    }

    void RowMap::UpdateLine(const TextBuffer& buffer, std::size_t line)
    {
        if (line >= m_rowsPerLine.size())
            return;

        m_breaks[line].clear();

        if (m_columnsPerRow != kNoWrap)
            m_breaks[line] = WrapColumns(buffer.LineText(line), m_columnsPerRow, m_tabWidth);

        SetCount(line, m_breaks[line].size() + 1);
    }

    void RowMap::SetRowsOf(std::size_t line, std::size_t rows)
    {
        if (line < m_rowsPerLine.size())
            SetCount(line, rows);
    }

    void RowMap::SetCount(std::size_t line, std::size_t rows)
    {
        const std::size_t previous = m_rowsPerLine[line];

        if (rows == previous)
            return;

        m_rowsPerLine[line] = rows;
        m_totalRows = m_totalRows - previous + rows;

        // Fenwick update. The delta is computed as an unsigned difference in the direction
        // that keeps it positive, with `grew` recording which way it went, rather than
        // relying on signed arithmetic: a line that shrank subtracts its delta from every
        // covering node, and an unsigned wraparound here would corrupt every later lookup
        // rather than failing loudly.
        const bool grew = rows > previous;
        const std::size_t delta = grew ? rows - previous : previous - rows;

        for (std::size_t index = line + 1; index < m_tree.size(); index += index & (~index + 1))
        {
            if (grew)
                m_tree[index] += delta;
            else
                m_tree[index] -= delta;
        }
    }

    std::size_t RowMap::PrefixSum(std::size_t line) const
    {
        std::size_t sum = 0;

        for (std::size_t index = line; index > 0; index -= index & (~index + 1))
            sum += m_tree[index];

        return sum;
    }

    std::size_t RowMap::FirstRowOf(std::size_t line) const
    {
        return PrefixSum(std::min(line, m_rowsPerLine.size()));
    }

    std::size_t RowMap::RowsOf(std::size_t line) const
    {
        return line < m_rowsPerLine.size() ? m_rowsPerLine[line] : 0;
    }

    const std::vector<std::size_t>& RowMap::BreaksOf(std::size_t line) const
    {
        return line < m_breaks.size() ? m_breaks[line] : kNoBreaks;
    }

    RowRef RowMap::RowAt(std::size_t row) const
    {
        if (m_rowsPerLine.empty())
            return RowRef{};

        // A row past the end resolves to the last line's last row, matching how the widget
        // already clamps a click below the document rather than treating it as an error.
        if (row >= m_totalRows)
        {
            const std::size_t last = m_rowsPerLine.size() - 1;
            const std::size_t rows = m_rowsPerLine[last];

            return RowRef{last, rows > 0 ? rows - 1 : 0};
        }

        // Fenwick descent: walk the tree from the highest power of two downwards, taking
        // a branch whenever the rows it covers still fit below `row`. Lands on the last
        // line whose first row is at or before `row`, skipping lines of zero rows for
        // free, since they add nothing to take.
        std::size_t index = 0;
        std::size_t remaining = row;
        std::size_t step = 1;

        while (step * 2 <= m_tree.size() - 1)
            step *= 2;

        for (; step > 0; step /= 2)
        {
            const std::size_t next = index + step;

            if (next < m_tree.size() && m_tree[next] <= remaining)
            {
                index = next;
                remaining -= m_tree[next];
            }
        }

        return RowRef{index, remaining};
    }
}
