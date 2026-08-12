#include "corepch.h"

#include "Core/Text/RowMap.h"

#include "Core/Text/TextBuffer.h"
#include "Core/Text/WrapLayout.h"

#include <algorithm>

namespace Core
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

        // The counts are filled in directly and the tree built once from them, the same
        // way the splices below do it. Doing this by repeated SetCount would be a
        // logarithmic update per line for a tree that is thrown away and rebuilt whole
        // anyway, and RebuildTree is already the trusted path.
        for (std::size_t line = 0; line < lines; ++line)
        {
            if (columnsPerRow != kNoWrap)
                m_breaks[line] = WrapColumns(buffer.LineText(line), columnsPerRow, tabWidth);

            m_rowsPerLine[line] = m_breaks[line].size() + 1;
        }

        RebuildTree();
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

    void RowMap::InsertLines(const TextBuffer& buffer, std::size_t at, std::size_t count)
    {
        if (count == 0)
            return;

        const std::size_t where = std::min(at, m_rowsPerLine.size());
        const auto offset = static_cast<std::ptrdiff_t>(where);

        m_rowsPerLine.insert(m_rowsPerLine.begin() + offset, count, 0);
        m_breaks.insert(m_breaks.begin() + offset, count, std::vector<std::size_t>{});

        for (std::size_t line = where; line < where + count; ++line)
        {
            if (m_columnsPerRow != kNoWrap && line < buffer.LineCount())
                m_breaks[line] = WrapColumns(buffer.LineText(line), m_columnsPerRow, m_tabWidth);

            m_rowsPerLine[line] = m_breaks[line].size() + 1;
        }

        RebuildTree();
    }

    void RowMap::RemoveLines(std::size_t at, std::size_t count)
    {
        if (count == 0 || at >= m_rowsPerLine.size())
            return;

        const std::size_t end = std::min(at + count, m_rowsPerLine.size());
        const auto first = static_cast<std::ptrdiff_t>(at);
        const auto last = static_cast<std::ptrdiff_t>(end);

        m_rowsPerLine.erase(m_rowsPerLine.begin() + first, m_rowsPerLine.begin() + last);
        m_breaks.erase(m_breaks.begin() + first, m_breaks.begin() + last);

        RebuildTree();
    }

    void RowMap::RebuildTree()
    {
        const std::size_t lines = m_rowsPerLine.size();

        m_tree.assign(lines + 1, 0);
        m_totalRows = 0;

        // Linear Fenwick construction: each node takes its own line's count and then hands
        // its subtotal to its parent, so the whole tree is one pass rather than one
        // logarithmic update per line.
        for (std::size_t index = 1; index <= lines; ++index)
        {
            m_tree[index] += m_rowsPerLine[index - 1];
            m_totalRows += m_rowsPerLine[index - 1];

            const std::size_t parent = index + (index & (~index + 1));

            if (parent <= lines)
                m_tree[parent] += m_tree[index];
        }
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
