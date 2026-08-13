#include "corepch.h"

#include "Core/Text/RowMap.h"

#include "Core/Text/TextBuffer.h"
#include "Core/Text/WrapLayout.h"

#include <algorithm>

namespace Core {

	namespace {
		const std::vector<std::size_t> kNoBreaks;
	}

	void RowMap::Rebuild(const TextBuffer& buffer, std::size_t columnsPerRow,
						 std::size_t tabWidth)
	{
		m_ColumnsPerRow = columnsPerRow;
		m_TabWidth = tabWidth;

		const std::size_t lines = buffer.GetLineCount();

		m_RowsPerLine.assign(lines, 0);
		m_Breaks.assign(lines, {});

		// The counts are filled in directly and the tree built once from them: SetCount per
		// line would be a logarithmic update each time into a tree about to be thrown away.
		for (std::size_t line = 0; line < lines; ++line)
		{
			if (columnsPerRow != kNoWrap)
				m_Breaks[line] = WrapColumns(buffer.LineText(line), columnsPerRow, tabWidth);

			m_RowsPerLine[line] = m_Breaks[line].size() + 1;
		}

		RebuildTree();
	}

	void RowMap::UpdateLine(const TextBuffer& buffer, std::size_t line)
	{
		if (line >= m_RowsPerLine.size())
			return;

		m_Breaks[line].clear();

		if (m_ColumnsPerRow != kNoWrap)
			m_Breaks[line] = WrapColumns(buffer.LineText(line), m_ColumnsPerRow, m_TabWidth);

		SetCount(line, m_Breaks[line].size() + 1);
	}

	void RowMap::InsertLines(const TextBuffer& buffer, std::size_t at, std::size_t count)
	{
		if (count == 0)
			return;

		const std::size_t where = std::min(at, m_RowsPerLine.size());
		const auto offset = static_cast<std::ptrdiff_t>(where);

		m_RowsPerLine.insert(m_RowsPerLine.begin() + offset, count, 0);
		m_Breaks.insert(m_Breaks.begin() + offset, count, std::vector<std::size_t>{});

		for (std::size_t line = where; line < where + count; ++line)
		{
			if (m_ColumnsPerRow != kNoWrap && line < buffer.GetLineCount())
				m_Breaks[line] = WrapColumns(buffer.LineText(line), m_ColumnsPerRow, m_TabWidth);

			m_RowsPerLine[line] = m_Breaks[line].size() + 1;
		}

		RebuildTree();
	}

	void RowMap::RemoveLines(std::size_t at, std::size_t count)
	{
		if (count == 0 || at >= m_RowsPerLine.size())
			return;

		const std::size_t end = std::min(at + count, m_RowsPerLine.size());
		const auto first = static_cast<std::ptrdiff_t>(at);
		const auto last = static_cast<std::ptrdiff_t>(end);

		m_RowsPerLine.erase(m_RowsPerLine.begin() + first, m_RowsPerLine.begin() + last);
		m_Breaks.erase(m_Breaks.begin() + first, m_Breaks.begin() + last);

		RebuildTree();
	}

	void RowMap::RebuildTree()
	{
		const std::size_t lines = m_RowsPerLine.size();

		m_Tree.assign(lines + 1, 0);
		m_TotalRows = 0;

		// Linear Fenwick construction: each node takes its own line's count and hands its
		// subtotal to its parent, so the whole tree is one pass.
		for (std::size_t index = 1; index <= lines; ++index)
		{
			m_Tree[index] += m_RowsPerLine[index - 1];
			m_TotalRows += m_RowsPerLine[index - 1];

			const std::size_t parent = index + (index & (~index + 1));

			if (parent <= lines)
				m_Tree[parent] += m_Tree[index];
		}
	}

	void RowMap::SetRowsOf(std::size_t line, std::size_t rows)
	{
		if (line < m_RowsPerLine.size())
			SetCount(line, rows);
	}

	void RowMap::SetCount(std::size_t line, std::size_t rows)
	{
		const std::size_t previous = m_RowsPerLine[line];

		if (rows == previous)
			return;

		m_RowsPerLine[line] = rows;
		m_TotalRows = m_TotalRows - previous + rows;

		// Fenwick update, with the delta kept positive and its direction carried separately:
		// an unsigned wraparound here would corrupt every later lookup rather than fail
		// loudly.
		const bool grew = rows > previous;
		const std::size_t delta = grew ? rows - previous : previous - rows;

		for (std::size_t index = line + 1; index < m_Tree.size(); index += index & (~index + 1))
		{
			if (grew)
				m_Tree[index] += delta;
			else
				m_Tree[index] -= delta;
		}
	}

	std::size_t RowMap::PrefixSum(std::size_t line) const
	{
		std::size_t sum = 0;

		for (std::size_t index = line; index > 0; index -= index & (~index + 1))
			sum += m_Tree[index];

		return sum;
	}

	std::size_t RowMap::FirstRowOf(std::size_t line) const
	{
		return PrefixSum(std::min(line, m_RowsPerLine.size()));
	}

	std::size_t RowMap::RowsOf(std::size_t line) const
	{
		return line < m_RowsPerLine.size() ? m_RowsPerLine[line] : 0;
	}

	const std::vector<std::size_t>& RowMap::BreaksOf(std::size_t line) const
	{
		return line < m_Breaks.size() ? m_Breaks[line] : kNoBreaks;
	}

	RowRef RowMap::RowAt(std::size_t row) const
	{
		if (m_RowsPerLine.empty())
			return RowRef{};

		// A row past the end resolves to the last line's last row, the way a click below the
		// document is already clamped rather than treated as an error.
		if (row >= m_TotalRows)
		{
			const std::size_t last = m_RowsPerLine.size() - 1;
			const std::size_t rows = m_RowsPerLine[last];

			return RowRef{last, rows > 0 ? rows - 1 : 0};
		}

		// Fenwick descent from the highest power of two downwards, taking a branch whenever
		// the rows it covers still fit below `row`, which lands on the last line whose first
		// row is at or before it.
		std::size_t index = 0;
		std::size_t remaining = row;
		std::size_t step = 1;

		while (step * 2 <= m_Tree.size() - 1)
			step *= 2;

		for (; step > 0; step /= 2)
		{
			const std::size_t next = index + step;

			if (next < m_Tree.size() && m_Tree[next] <= remaining)
			{
				index = next;
				remaining -= m_Tree[next];
			}
		}

		return RowRef{index, remaining};
	}
}