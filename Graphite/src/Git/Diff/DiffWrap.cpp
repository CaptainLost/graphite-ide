#include "Git/Diff/DiffWrap.h"

#include "Core/Text/Encoding.h"

#include <algorithm>

namespace Graphite {

	namespace {

		constexpr std::size_t kImGuiTabCells = 4;
	}

	std::vector<std::size_t> WrapBytes(std::string_view text, std::size_t columns)
	{
		std::vector<std::size_t> breaks;

		constexpr std::size_t minimumColumns = 1;
		const std::size_t width = std::max<std::size_t>(columns, minimumColumns);

		std::size_t rowStart = 0;
		std::size_t cellsOnRow = 0;
		std::size_t afterLastSpace = rowStart;

		std::size_t position = 0;

		while (position < text.size())
		{
			const bool isTab = text[position] == '\t';
			const std::size_t nextPosition =
				isTab ? position + 1 : Core::NextCharacter(text, position);
			const std::size_t advance = isTab ? kImGuiTabCells : 1;
			const bool fitsOnRow = cellsOnRow + advance <= width;
			const bool isFirstCharacterOfRow = position == rowStart;

			if (fitsOnRow || isFirstCharacterOfRow)
			{
				if (text[position] == ' ')
				{
					afterLastSpace = nextPosition;
				}

				cellsOnRow += advance;
				position = nextPosition;
				continue;
			}

			std::size_t breakPosition = position;

			if (text[position] == ' ')
			{
				while (breakPosition < text.size() && text[breakPosition] == ' ')
				{
					++breakPosition;
				}
			}
			else if (afterLastSpace > rowStart)
			{
				breakPosition = afterLastSpace;
			}

			if (breakPosition >= text.size())
			{
				break;
			}

			breaks.push_back(breakPosition);

			rowStart = breakPosition;
			afterLastSpace = breakPosition;
			cellsOnRow = 0;
			position = breakPosition;
		}

		return breaks;
	}

	WrapSegment SegmentOf(std::string_view text, std::span<const std::size_t> breaks,
						  std::size_t index)
	{
		if (index > breaks.size())
		{
			return WrapSegment{text.size(), text.size()};
		}

		const std::size_t from = index == 0 ? 0 : breaks[index - 1];
		const std::size_t to = index < breaks.size() ? breaks[index] : text.size();

		return WrapSegment{from, to};
	}

	DiffRowLines DiffRowLines::Uniform(std::size_t rows)
	{
		DiffRowLines lines;
		lines.m_Rows = rows;

		return lines;
	}

	DiffRowLines DiffRowLines::FromCounts(std::span<const std::size_t> counts)
	{
		DiffRowLines lines;
		lines.m_Rows = counts.size();
		lines.m_Starts.reserve(counts.size() + 1);

		std::size_t total = 0;

		for (const std::size_t count : counts)
		{
			lines.m_Starts.push_back(total);
			total += std::max<std::size_t>(count, 1);
		}

		lines.m_Starts.push_back(total);

		return lines;
	}

	std::size_t DiffRowLines::TotalLines() const
	{
		return m_Starts.empty() ? m_Rows : m_Starts.back();
	}

	std::size_t DiffRowLines::FirstLineOf(std::size_t row) const
	{
		const std::size_t clamped = std::min(row, m_Rows);

		return m_Starts.empty() ? clamped : m_Starts[clamped];
	}

	std::size_t DiffRowLines::LinesOf(std::size_t row) const
	{
		if (row >= m_Rows)
		{
			return 0;
		}

		return m_Starts.empty() ? 1 : m_Starts[row + 1] - m_Starts[row];
	}

	std::size_t DiffRowLines::RowOfLine(std::size_t line) const
	{
		if (m_Rows == 0)
		{
			return 0;
		}

		if (m_Starts.empty())
		{
			return std::min(line, m_Rows - 1);
		}

		const std::span<const std::size_t> rowStarts(m_Starts.data(), m_Rows);
		const std::span<const std::size_t>::iterator firstRowStartingAfterLine =
			std::upper_bound(rowStarts.begin(), rowStarts.end(), line);

		return static_cast<std::size_t>(firstRowStartingAfterLine - rowStarts.begin()) - 1;
	}
}
