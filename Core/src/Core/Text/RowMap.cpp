#include "corepch.h"

#include "Core/Text/RowMap.h"

#include "Core/Jobs/CancellationPacing.h"
#include "Core/Jobs/CancellationToken.h"
#include "Core/Text/TextBuffer.h"
#include "Core/Text/WrapLayout.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace Core {

	namespace {
		const std::vector<std::size_t> kNoBreaks;

		constexpr std::size_t LowestSetBit(std::size_t value)
		{
			return value & (~value + 1);
		}

		static_assert(LowestSetBit(0b1100) == 0b0100);
		static_assert(LowestSetBit(0b1000) == 0b1000);

		std::size_t RowsOfLineWithBreaks(const std::vector<std::size_t>& breaks)
		{
			return breaks.size() + 1;
		}

		std::size_t HighestPowerOfTwoAtMost(std::size_t limit)
		{
			std::size_t power = 1;

			while (power * 2 <= limit)
			{
				power *= 2;
			}

			return power;
		}
	}

	void RowMap::Rebuild(const TextBuffer& buffer, std::size_t columnsPerRow,
						 std::size_t tabWidth)
	{
		const CancellationToken neverCancelled;

		RebuildUnlessCancelled(buffer, columnsPerRow, tabWidth, neverCancelled);
	}

	bool RowMap::RebuildUnlessCancelled(const TextBuffer& buffer, std::size_t columnsPerRow, std::size_t tabWidth,
										const CancellationToken& cancellation)
	{
		m_ColumnsPerRow = columnsPerRow;
		m_TabWidth = tabWidth;

		const std::size_t lineCount = buffer.GetLineCount();

		m_RowsPerLine.assign(lineCount, kNoRows);
		m_Breaks.assign(lineCount, kNoBreaks);

		// The counts are filled in directly and the tree built once from them: SetRowCount per line
		// would be a logarithmic update each time into a tree about to be thrown away.
		CancellationPacing pacing{cancellation};

		for (std::size_t line = 0; line < lineCount; ++line)
		{
			if (pacing.CancelledBefore(buffer.LineLength(line)))
			{
				return false;
			}

			if (columnsPerRow != kNoWrap)
			{
				std::optional<std::vector<std::size_t>> breaks =
					WrapColumnsUnlessCancelled(buffer.LineText(line), columnsPerRow, tabWidth, cancellation);

				if (!breaks)
				{
					return false;
				}

				m_Breaks[line] = std::move(*breaks);
			}

			m_RowsPerLine[line] = RowsOfLineWithBreaks(m_Breaks[line]);
		}

		RebuildTree();

		return true;
	}

	void RowMap::UpdateLine(const TextBuffer& buffer, std::size_t line)
	{
		if (line >= m_RowsPerLine.size())
		{
			return;
		}

		m_Breaks[line].clear();

		if (m_ColumnsPerRow != kNoWrap)
		{
			m_Breaks[line] = WrapColumns(buffer.LineText(line), m_ColumnsPerRow, m_TabWidth);
		}

		SetRowCount(line, RowsOfLineWithBreaks(m_Breaks[line]));
	}

	void RowMap::InsertLines(const TextBuffer& buffer, std::size_t firstNewLine, std::size_t newLineCount)
	{
		if (newLineCount == 0)
		{
			return;
		}

		const std::size_t insertionLine = std::min(firstNewLine, m_RowsPerLine.size());
		const auto insertionOffset = static_cast<std::ptrdiff_t>(insertionLine);

		m_RowsPerLine.insert(m_RowsPerLine.begin() + insertionOffset, newLineCount, kNoRows);
		m_Breaks.insert(m_Breaks.begin() + insertionOffset, newLineCount, kNoBreaks);

		for (std::size_t line = insertionLine; line < insertionLine + newLineCount; ++line)
		{
			if (m_ColumnsPerRow != kNoWrap && line < buffer.GetLineCount())
			{
				m_Breaks[line] = WrapColumns(buffer.LineText(line), m_ColumnsPerRow, m_TabWidth);
			}

			m_RowsPerLine[line] = RowsOfLineWithBreaks(m_Breaks[line]);
		}

		RebuildTree();
	}

	void RowMap::RemoveLines(std::size_t firstRemovedLine, std::size_t removedLineCount)
	{
		if (removedLineCount == 0 || firstRemovedLine >= m_RowsPerLine.size())
		{
			return;
		}

		const std::size_t endLine = std::min(firstRemovedLine + removedLineCount, m_RowsPerLine.size());
		const auto eraseBegin = static_cast<std::ptrdiff_t>(firstRemovedLine);
		const auto eraseEnd = static_cast<std::ptrdiff_t>(endLine);

		m_RowsPerLine.erase(m_RowsPerLine.begin() + eraseBegin, m_RowsPerLine.begin() + eraseEnd);
		m_Breaks.erase(m_Breaks.begin() + eraseBegin, m_Breaks.begin() + eraseEnd);

		RebuildTree();
	}

	void RowMap::RebuildTree()
	{
		const std::size_t lineCount = m_RowsPerLine.size();

		m_FenwickTree.assign(lineCount + 1, kNoRows);
		m_TotalRows = 0;

		for (std::size_t node = 1; node <= lineCount; ++node)
		{
			const std::size_t lineOfNode = node - 1;
			m_FenwickTree[node] += m_RowsPerLine[lineOfNode];
			m_TotalRows += m_RowsPerLine[lineOfNode];

			const std::size_t parent = node + LowestSetBit(node);

			if (parent <= lineCount)
			{
				m_FenwickTree[parent] += m_FenwickTree[node];
			}
		}
	}

	void RowMap::SetRowsOf(std::size_t line, std::size_t rowCount)
	{
		if (line < m_RowsPerLine.size())
		{
			SetRowCount(line, rowCount);
		}
	}

	void RowMap::SetRowCount(std::size_t line, std::size_t rowCount)
	{
		const std::size_t previousRowCount = m_RowsPerLine[line];

		if (rowCount == previousRowCount)
		{
			return;
		}

		m_RowsPerLine[line] = rowCount;
		m_TotalRows = m_TotalRows - previousRowCount + rowCount;

		const bool grew = rowCount > previousRowCount;
		const std::size_t delta = grew ? rowCount - previousRowCount : previousRowCount - rowCount;

		for (std::size_t node = line + 1; node < m_FenwickTree.size(); node += LowestSetBit(node))
		{
			if (grew)
			{
				m_FenwickTree[node] += delta;
			}
			else
			{
				m_FenwickTree[node] -= delta;
			}
		}
	}

	std::size_t RowMap::CountRowsBefore(std::size_t line) const
	{
		std::size_t rowsBefore = 0;

		for (std::size_t node = line; node > 0; node -= LowestSetBit(node))
		{
			rowsBefore += m_FenwickTree[node];
		}

		return rowsBefore;
	}

	std::size_t RowMap::FirstRowOf(std::size_t line) const
	{
		return CountRowsBefore(std::min(line, m_RowsPerLine.size()));
	}

	std::size_t RowMap::RowsOf(std::size_t line) const
	{
		return line < m_RowsPerLine.size() ? m_RowsPerLine[line] : kNoRows;
	}

	const std::vector<std::size_t>& RowMap::BreaksOf(std::size_t line) const
	{
		return line < m_Breaks.size() ? m_Breaks[line] : kNoBreaks;
	}

	RowRef RowMap::RowAt(std::size_t row) const
	{
		if (m_RowsPerLine.empty())
		{
			return RowRef{};
		}

		const bool rowIsPastTheEnd = row >= m_TotalRows;

		if (rowIsPastTheEnd)
		{
			const std::size_t lastLine = m_RowsPerLine.size() - 1;
			const std::size_t lastLineRows = m_RowsPerLine[lastLine];
			const std::size_t lastSubRow = lastLineRows > 0 ? lastLineRows - 1 : 0;

			return RowRef{.Line = lastLine, .SubRow = lastSubRow};
		}

		std::size_t linesBefore = 0;
		std::size_t rowsLeft = row;

		for (std::size_t step = HighestPowerOfTwoAtMost(m_FenwickTree.size() - 1); step > 0; step /= 2)
		{
			const std::size_t candidate = linesBefore + step;
			const bool candidateEndsAtOrBeforeRow =
				candidate < m_FenwickTree.size() && m_FenwickTree[candidate] <= rowsLeft;

			if (candidateEndsAtOrBeforeRow)
			{
				linesBefore = candidate;
				rowsLeft -= m_FenwickTree[candidate];
			}
		}

		return RowRef{.Line = linesBefore, .SubRow = rowsLeft};
	}
}