#include "corepch.h"

#include "Core/Text/WrapLayout.h"

#include "Core/Text/Columns.h"
#include "Core/Text/Encoding.h"
#include "Core/Text/RowMap.h"
#include "Core/Text/TextBuffer.h"

#include <algorithm>
#include <string>

namespace Core {

	namespace {
		struct RowScan
		{
			std::size_t At = 0;
			std::size_t Column = 0;

			bool HaveSpace = false;
			std::size_t SpaceByte = 0;
			std::size_t SpaceColumn = 0;

			// The character occupying the limit's last cell, and the boundary just past it.
			// A tab occupies a run of cells, so the character covering that cell is not the
			// same thing as the character starting on it.
			bool HaveEdge = false;
			bool EdgeIsTab = false;
			std::size_t EdgeByte = 0;
			std::size_t EdgeColumn = 0;
			std::size_t PastEdgeByte = 0;
			std::size_t PastEdgeColumn = 0;
		};

		// The byte and the column are carried together because deriving one from the other
		// per probe re-walks the line from byte 0 and makes wrapping a single line quadratic
		// in its length. Both only ever land on a character boundary, so a row can never
		// begin inside a multi-byte character or inside a tab's run.
		RowScan ScanToLimit(std::string_view line, std::size_t tabWidth, std::size_t limit,
							std::size_t at, std::size_t column)
		{
			RowScan scan;

			while (at < line.size() && column < limit)
			{
				const bool isTab = line[at] == '\t';
				const std::size_t nextByte = isTab ? at + 1 : NextCharacter(line, at);
				const std::size_t nextColumn =
					isTab ? ColumnAfterTab(column, tabWidth) : column + 1;

				if (!isTab && line[at] == ' ')
				{
					scan.SpaceByte = at;
					scan.SpaceColumn = column;
					scan.HaveSpace = true;
				}

				if (column <= limit - 1 && limit - 1 < nextColumn)
				{
					scan.EdgeByte = at;
					scan.EdgeColumn = column;
					scan.PastEdgeByte = nextByte;
					scan.PastEdgeColumn = nextColumn;
					scan.EdgeIsTab = isTab;
					scan.HaveEdge = true;
				}

				at = nextByte;
				column = nextColumn;
			}

			scan.At = at;
			scan.Column = column;

			return scan;
		}

		// The run is invisible where a row starting with one is not, so a run reaching past
		// the limit stays on the row it ends even though that makes the row visually wider
		// than columnsPerRow.
		void SkipSpaceRun(std::string_view line, std::size_t total, std::size_t& at,
						  std::size_t& column)
		{
			while (at < line.size() && column < total && line[at] == ' ')
			{
				++at;
				++column;
			}
		}

		std::pair<std::size_t, std::size_t> SubRowOfColumn(
			const std::vector<std::size_t>& breaks, std::size_t column)
		{
			std::size_t subRow = 0;
			std::size_t rowStart = 0;

			// "At or before" rather than "before" is the whole rule: a column equal to a
			// break is the first cell of the row that break starts.
			for (std::size_t index = 0; index < breaks.size(); ++index)
			{
				if (breaks[index] > column)
				{
					break;
				}

				subRow = index + 1;
				rowStart = breaks[index];
			}

			return {subRow, rowStart};
		}
	}

	std::vector<std::size_t> WrapColumns(std::string_view line, std::size_t columnsPerRow,
										 std::size_t tabWidth)
	{
		std::vector<std::size_t> breaks;

		// A row narrower than one cell would make the walk below stand still.
		const std::size_t width = std::max<std::size_t>(columnsPerRow, 1);
		const std::size_t total = ColumnCount(line, tabWidth);

		std::size_t at = 0;
		std::size_t column = 0;
		std::size_t rowStart = 0;

		while (total - rowStart > width)
		{
			const std::size_t limit = rowStart + width;
			const RowScan scan = ScanToLimit(line, tabWidth, limit, at, column);

			at = scan.At;
			column = scan.Column;

			// The loop condition guarantees the line reaches past the limit, so the scan
			// always covers its last cell; stopping rather than guessing if it somehow did
			// not is what keeps this loop from standing still.
			if (!scan.HaveEdge)
			{
				break;
			}

			std::size_t breakAt = 0;

			if (scan.HaveSpace)
			{
				at = scan.SpaceByte + 1;
				column = scan.SpaceColumn + 1;

				SkipSpaceRun(line, total, at, column);

				breakAt = column;
			}
			else if (scan.EdgeIsTab && scan.EdgeColumn != rowStart)
			{
				// The hard break would land inside a tab's run, so the whole tab goes to the
				// next row.
				at = scan.EdgeByte;
				column = scan.EdgeColumn;
				breakAt = scan.EdgeColumn;
			}
			else
			{
				// A single word wider than the row: breaking hard at the limit keeps it on
				// screen. A tab starting on this row's own start is wider than the whole row,
				// so the break goes after it — there is nowhere further back to go.
				at = scan.PastEdgeByte;
				column = scan.PastEdgeColumn;
				breakAt = scan.PastEdgeColumn;
			}

			if (breakAt >= total)
			{
				break;
			}

			breaks.push_back(breakAt);
			rowStart = breakAt;
		}

		return breaks;
	}

	VisualPosition CaretToVisual(const TextBuffer& buffer, const RowMap& rows,
								 std::size_t offset, std::size_t tabWidth)
	{
		const Position position = buffer.ToPosition(offset);
		const std::size_t line = position.Line;
		const std::string text = buffer.LineText(line);
		const std::size_t column = ColumnOfByte(text, position.Column, tabWidth);

		const auto [subRow, rowStart] = SubRowOfColumn(rows.BreaksOf(line), column);

		return VisualPosition{rows.FirstRowOf(line) + subRow, column - rowStart};
	}

	std::size_t VisualToCaret(const TextBuffer& buffer, const RowMap& rows,
							  VisualPosition position, std::size_t tabWidth)
	{
		const RowRef ref = rows.RowAt(position.Row);
		const std::string text = buffer.LineText(ref.Line);

		const std::vector<std::size_t>& breaks = rows.BreaksOf(ref.Line);

		// Guarded rather than indexed straight, because RowMap::SetRowsOf forces a row count
		// without touching the breaks: degrading to the line's own start keeps a decoupled
		// pair a wrong caret rather than a read past the end of the vector.
		const std::size_t rowStart =
			ref.SubRow == 0 || ref.SubRow > breaks.size() ? 0 : breaks[ref.SubRow - 1];

		// One less than the next row's first column, never the break itself: landing exactly
		// on a break is what SubRowOfColumn reads back as the *next* row's start, so the
		// row-end clamp has to agree with the row-start rule or the round trip breaks.
		const std::size_t rowEnd = ref.SubRow < breaks.size() ? breaks[ref.SubRow] - 1
															  : ColumnCount(text, tabWidth);

		const std::size_t column = std::min(rowStart + position.Column, rowEnd);

		return buffer.LineStart(ref.Line) + ByteOfColumn(text, column, tabWidth);
	}
}
