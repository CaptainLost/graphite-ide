#pragma once

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace Graphite {

	// Where a diff line's drawn rows after the first begin, as byte offsets into it; empty for a
	// line that fits in `columns` cells.
	//
	// Not Core::WrapColumns, which the editor wraps with: that one counts a tab to the next tab
	// stop, because the editor draws tabs expanded to them. The diff draws a line as it is, and
	// ImGui draws a tab four cells wide wherever it falls, so breaking where the editor would
	// leaves a tabbed row hanging past the pane's edge. The rule is otherwise the editor's: break
	// after the last space on the row, keeping a run of spaces that reaches past the edge on the
	// row it ends, and break a word wider than the whole row hard at the edge.
	std::vector<std::size_t> WrapBytes(std::string_view text, std::size_t columns);

	// The bytes [From, To) of `text` its drawn row `index` shows, given its WrapBytes.
	struct WrapSegment
	{
		std::size_t From = 0;
		std::size_t To = 0;
	};

	WrapSegment SegmentOf(std::string_view text, std::span<const std::size_t> breaks,
						  std::size_t index);

	// How many screen lines each row of a diff takes, and on which one each row starts. One
	// each while nothing wraps, which needs no table at all — a diff of a generated file runs to
	// tens of thousands of rows, and a table of ones for each would be built for nothing.
	class DiffRowLines
	{
	public:
		DiffRowLines() = default;

		static DiffRowLines Uniform(std::size_t rows);

		// Every count at least one: a row takes its line even with nothing to draw on it.
		static DiffRowLines FromCounts(std::span<const std::size_t> counts);

		std::size_t RowCount() const { return m_Rows; }
		std::size_t TotalLines() const;

		std::size_t FirstLineOf(std::size_t row) const;
		std::size_t LinesOf(std::size_t row) const;

		// The row screen line `line` belongs to, clamped to the last row. Zero without rows.
		std::size_t RowOfLine(std::size_t line) const;

	private:
		std::size_t m_Rows = 0;

		// Empty while every row takes one line; otherwise the first line of each row and,
		// last, the total.
		std::vector<std::size_t> m_Starts;
	};
}
