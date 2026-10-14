#pragma once

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace Graphite {

	// WARNING: This is not Core::WrapColumns. The editor counts a tab up to the next tab stop;
	// the diff draws a line as it is and ImGui draws a tab four cells wide wherever it falls, so
	// breaking as the editor does leaves a tabbed row hanging past the pane's edge.
	std::vector<std::size_t> WrapBytes(std::string_view text, std::size_t columns);

	struct WrapSegment
	{
		std::size_t From = 0;
		std::size_t To = 0;
	};

	WrapSegment SegmentOf(std::string_view text, std::span<const std::size_t> breaks,
						  std::size_t index);

	class DiffRowLines
	{
	public:
		DiffRowLines() = default;

		static DiffRowLines Uniform(std::size_t rows);

		static DiffRowLines FromCounts(std::span<const std::size_t> counts);

		std::size_t RowCount() const { return m_Rows; }
		std::size_t TotalLines() const;

		std::size_t FirstLineOf(std::size_t row) const;
		std::size_t LinesOf(std::size_t row) const;

		std::size_t RowOfLine(std::size_t line) const;

	private:
		std::size_t m_Rows = 0;

		// WARNING: Empty while every row takes one line, so a diff of tens of thousands of rows
		// builds no table. Otherwise it holds the first line of each row and, last, the total.
		std::vector<std::size_t> m_Starts;
	};
}
