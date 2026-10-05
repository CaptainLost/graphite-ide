#include "Git/Diff/DiffWrap.h"

#include "Core/Text/Encoding.h"

#include <algorithm>

namespace Graphite {

	namespace {

		// What ImGui advances a tab by: four spaces, wherever it falls.
		constexpr std::size_t kTabCells = 4;
	}

	std::vector<std::size_t> WrapBytes(std::string_view text, std::size_t columns)
	{
		std::vector<std::size_t> breaks;

		// A row narrower than one cell would never let the walk below move on.
		const std::size_t width = std::max<std::size_t>(columns, 1);

		std::size_t rowStart = 0;
		std::size_t cells = 0;

		// Just past the last space on the row, where a word that does not fit moves down from;
		// rowStart while the row has none.
		std::size_t afterSpace = rowStart;

		std::size_t at = 0;

		while (at < text.size())
		{
			const bool isTab = text[at] == '\t';
			const std::size_t next = isTab ? at + 1 : Core::NextCharacter(text, at);
			const std::size_t advance = isTab ? kTabCells : 1;

			// The row's first character stays on it however wide it is: anywhere else it would
			// leave an empty row behind and meet the same edge on the next.
			if (cells + advance <= width || at == rowStart)
			{
				if (text[at] == ' ')
				{
					afterSpace = next;
				}

				cells += advance;
				at = next;
				continue;
			}

			std::size_t breakAt = at;

			if (text[at] == ' ')
			{
				while (breakAt < text.size() && text[breakAt] == ' ')
				{
					++breakAt;
				}
			}
			else if (afterSpace > rowStart)
			{
				breakAt = afterSpace;
			}

			if (breakAt >= text.size())
			{
				break;
			}

			breaks.push_back(breakAt);

			// From the break, not from here: a word moved down is measured again on its new row.
			rowStart = breakAt;
			afterSpace = breakAt;
			cells = 0;
			at = breakAt;
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

		// The last row starting at or before the line; the total at the back is never one.
		const auto starts = std::span(m_Starts).first(m_Rows);
		const auto after = std::upper_bound(starts.begin(), starts.end(), line);

		return static_cast<std::size_t>(after - starts.begin()) - 1;
	}
}
