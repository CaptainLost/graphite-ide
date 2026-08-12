#include "corepch.h"

#include "Core/Text/WrapLayout.h"

#include "Core/Text/Columns.h"
#include "Core/Text/Encoding.h"
#include "Core/Text/RowMap.h"
#include "Core/Text/TextBuffer.h"

#include <algorithm>
#include <string>

namespace Core {

	std::vector<std::size_t> WrapColumns(std::string_view line, std::size_t columnsPerRow,
										 std::size_t tabWidth)
	{
		std::vector<std::size_t> breaks;

		// A row narrower than one cell would make the walk below stand still. One column
		// is the narrowest width that still terminates, and a window that small is
		// unreadable either way.
		const std::size_t width = std::max<std::size_t>(columnsPerRow, 1);
		const std::size_t total = ColumnCount(line, tabWidth);

		// One forward walk over the whole line, carrying the byte and the column
		// together — the shape ExpandTabs uses. Deriving one from the other per probe
		// (a ByteOfColumn call inside the backward space search below) re-walks the line
		// from byte 0 every time and makes wrapping a single line quadratic in its
		// length: measured at 7 seconds for a 128 KB minified line, with the frame
		// unable to end, and four times that per doubling.
		//
		// `at` is the byte the walk sits on and `column` the column that byte starts at.
		// Both only ever move to a character boundary, so a row can never begin inside a
		// multi-byte character or inside a tab's run.
		std::size_t at = 0;
		std::size_t column = 0;
		std::size_t rowStart = 0;

		while (total - rowStart > width)
		{
			const std::size_t limit = rowStart + width;

			// The last space starting at or before the limit's last cell.
			std::size_t spaceByte = 0;
			std::size_t spaceColumn = 0;
			bool haveSpace = false;

			// The character occupying the limit's last cell, and the boundary just past
			// it. A tab occupies a run of cells, so the character covering that cell is
			// not the same thing as the character starting on it.
			std::size_t edgeByte = 0;
			std::size_t edgeColumn = 0;
			std::size_t pastEdgeByte = 0;
			std::size_t pastEdgeColumn = 0;
			bool edgeIsTab = false;
			bool haveEdge = false;

			while (at < line.size() && column < limit)
			{
				const bool isTab = line[at] == '\t';
				const std::size_t nextByte = isTab ? at + 1 : NextCharacter(line, at);
				const std::size_t nextColumn =
					isTab ? ColumnAfterTab(column, tabWidth) : column + 1;

				if (!isTab && line[at] == ' ')
				{
					spaceByte = at;
					spaceColumn = column;
					haveSpace = true;
				}

				if (column <= limit - 1 && limit - 1 < nextColumn)
				{
					edgeByte = at;
					edgeColumn = column;
					pastEdgeByte = nextByte;
					pastEdgeColumn = nextColumn;
					edgeIsTab = isTab;
					haveEdge = true;
				}

				at = nextByte;
				column = nextColumn;
			}

			// The loop condition guarantees the line reaches past the limit, so the walk
			// above always covers the limit's last cell. Stopping rather than guessing if
			// it somehow did not is what keeps this loop from standing still.
			if (!haveEdge)
				break;

			std::size_t breakAt = 0;

			if (haveSpace)
			{
				// Break after the space, then skip the whole run of spaces so the next row
				// starts on the first non-space character. A run reaching past the limit
				// stays on the row it ends even though that makes the row visually wider
				// than columnsPerRow — the run is invisible, a row starting with one is
				// not. The skip can only move forward from the limit: any earlier space
				// would not have been the last one.
				at = spaceByte + 1;
				column = spaceColumn + 1;

				while (at < line.size() && column < total && line[at] == ' ')
				{
					++at;
					++column;
				}

				breakAt = column;
			}
			else if (edgeIsTab && edgeColumn != rowStart)
			{
				// No space to break on and the hard break would land inside a tab's run:
				// move back to where the tab starts so the whole tab goes to the next row.
				at = edgeByte;
				column = edgeColumn;
				breakAt = edgeColumn;
			}
			else
			{
				// No space to break on: a single word wider than the row. Breaking hard at
				// the limit keeps it on screen instead of letting it run off the edge —
				// and for a non-tab character the cell past the limit's last one *is* the
				// limit. When the limit's last cell belongs to a tab starting at this
				// row's own start, that tab is wider than the whole row, so the break goes
				// after it instead; there is nowhere further back to go.
				at = pastEdgeByte;
				column = pastEdgeColumn;
				breakAt = pastEdgeColumn;
			}

			// If the adjusted break is at or past the end of the line, there is nothing
			// left to put on another row; stop without pushing a break.
			if (breakAt >= total)
				break;

			breaks.push_back(breakAt);
			rowStart = breakAt;
		}

		return breaks;
	}

	VisualPosition CaretToVisual(const TextBuffer& buffer, const RowMap& rows,
								 std::size_t offset, std::size_t tabWidth)
	{
		const Position position = buffer.ToPosition(offset);
		const std::size_t line = position.line;
		const std::string text = buffer.LineText(line);
		const std::size_t column = ColumnOfByte(text, position.column, tabWidth);

		const std::vector<std::size_t>& breaks = rows.BreaksOf(line);

		// The last break at or before this column is the row the offset sits on. "At or
		// before" rather than "before" is the whole rule: a column equal to a break is the
		// first cell of the row that break starts.
		std::size_t subRow = 0;
		std::size_t rowStart = 0;

		for (std::size_t index = 0; index < breaks.size(); ++index)
		{
			if (breaks[index] > column)
				break;

			subRow = index + 1;
			rowStart = breaks[index];
		}

		return VisualPosition{rows.FirstRowOf(line) + subRow, column - rowStart};
	}

	std::size_t VisualToCaret(const TextBuffer& buffer, const RowMap& rows,
							  VisualPosition position, std::size_t tabWidth)
	{
		const RowRef ref = rows.RowAt(position.row);
		const std::string text = buffer.LineText(ref.line);

		const std::vector<std::size_t>& breaks = rows.BreaksOf(ref.line);

		// Guarded against a subRow the break list cannot account for, rather than indexed
		// straight: `rowsPerLine[line] == breaks[line].size() + 1` holds everywhere today,
		// but RowMap::SetRowsOf forces a count without touching the breaks and so can
		// decouple the two. Folding, in stage 5, is what will call it — a folded line set
		// to zero rows would send this read past the end of the vector. Degrading to the
		// line's own start keeps that a wrong caret rather than a crash.
		const std::size_t rowStart =
			ref.subRow == 0 || ref.subRow > breaks.size() ? 0 : breaks[ref.subRow - 1];

		// Where this row ends: the column just before the next row's first cell (a break
		// is the following row's own first column, never this row's), or the end of the
		// line for the last row. Using the break itself here — rather than one less than
		// it — would let the clamp below land exactly on the break, which CaretToVisual
		// then reads back as the *next* row's start: the row-end clamp has to agree with
		// the row-start rule above, or the round trip breaks.
		const std::size_t rowEnd = ref.subRow < breaks.size() ? breaks[ref.subRow] - 1
															  : ColumnCount(text, tabWidth);

		const std::size_t column = std::min(rowStart + position.column, rowEnd);

		return buffer.LineStart(ref.line) + ByteOfColumn(text, column, tabWidth);
	}
}