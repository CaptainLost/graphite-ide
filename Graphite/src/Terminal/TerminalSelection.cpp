#include "Terminal/TerminalSelection.h"

#include "Core/Text/Utf8.h"
#include "Core/Text/WordBounds.h"

#include <algorithm>
#include <cmath>

namespace Graphite {

	namespace {
		std::int64_t Signed(std::uint64_t value)
		{
			return static_cast<std::int64_t>(value);
		}

		// The oldest line the scrollback still holds.
		std::int64_t OldestLine(std::uint64_t rowsScrolledOff, std::size_t scrollbackCount)
		{
			return Signed(rowsScrolledOff) - static_cast<std::int64_t>(scrollbackCount);
		}

		std::size_t CellCount(const std::vector<Core::TerminalCell>& cells, std::size_t columns)
		{
			return std::min(columns, cells.size());
		}

		// A trailer stands for the character its leader holds.
		char32_t CodeAt(const std::vector<Core::TerminalCell>& cells, std::size_t column)
		{
			if (cells[column].WideTrailer && column > 0)
			{
				return cells[column - 1].Code;
			}

			return cells[column].Code;
		}

		bool IsBlank(const Core::TerminalScreen& screen, std::uint16_t row)
		{
			for (std::uint16_t column = 0; column < screen.GetColumns(); ++column)
			{
				if (screen.At(row, column).Code != U' ')
				{
					return false;
				}
			}

			return true;
		}
	}

	std::int64_t LineOfScrollbackRow(std::uint64_t rowsScrolledOff, std::size_t scrollbackCount,
									 std::size_t index)
	{
		return OldestLine(rowsScrolledOff, scrollbackCount) + static_cast<std::int64_t>(index);
	}

	std::int64_t LineOfScreenRow(std::uint64_t rowsScrolledOff, int screenRow)
	{
		return Signed(rowsScrolledOff) + screenRow;
	}

	bool SelectionLeftHistory(const Core::TextSelection& selection, std::uint64_t rowsScrolledOff,
							  std::size_t scrollbackCount)
	{
		return !selection.IsEmpty() &&
			   selection.Last().Line < OldestLine(rowsScrolledOff, scrollbackCount);
	}

	Core::TextSelection ClampSelectionToHistory(const Core::TextSelection& selection,
												std::uint64_t rowsScrolledOff, std::size_t scrollbackCount)
	{
		const std::int64_t oldest = OldestLine(rowsScrolledOff, scrollbackCount);

		if (selection.IsEmpty() || selection.First().Line >= oldest || selection.Last().Line < oldest)
		{
			return selection;
		}

		// The start is on a line that has gone and the end is not: the start moves to the head of
		// the oldest line held, and each end stays the end it was.
		const Core::TextPoint clamped{oldest, 0};
		const bool pressedIsFirst = selection.Pressed() == selection.First();

		Core::TextSelection result;
		result.Select(pressedIsFirst ? clamped : selection.Pressed(),
					  pressedIsFirst ? selection.Last() : clamped);
		return result;
	}

	std::vector<std::size_t> TerminalCellBytes(const std::vector<Core::TerminalCell>& cells, std::size_t columns)
	{
		std::vector<std::size_t> bytes(columns + 1, 0);
		const std::size_t count = CellCount(cells, columns);
		std::string scratch;
		std::size_t offset = 0;

		for (std::size_t column = 0; column < columns; ++column)
		{
			bytes[column] = offset;

			if (column < count && !cells[column].WideTrailer)
			{
				scratch.clear();
				Core::AppendUtf8(scratch, cells[column].Code);
				offset += scratch.size();
			}
		}

		bytes[columns] = offset;
		return bytes;
	}

	std::string TerminalRowText(const std::vector<Core::TerminalCell>& cells, std::size_t columns)
	{
		const std::size_t count = CellCount(cells, columns);
		std::string text;

		for (std::size_t column = 0; column < count; ++column)
		{
			// The trailer carries no character of its own; its leader already wrote the whole one.
			if (!cells[column].WideTrailer)
			{
				Core::AppendUtf8(text, cells[column].Code);
			}
		}

		return text;
	}

	Core::ColumnSpan TerminalWordAt(const std::vector<Core::TerminalCell>& cells, std::size_t columns,
									std::size_t column)
	{
		const std::size_t count = CellCount(cells, columns);

		if (column >= count)
		{
			return {count, count};
		}

		if (cells[column].WideTrailer && column > 0)
		{
			--column;
		}

		const auto isWord = [&cells](std::size_t at) {
			return Core::IsWordCodePoint(CodeAt(cells, at), Core::WordKind::Path);
		};

		std::size_t from = column;
		std::size_t to = column + 1;

		if (isWord(column))
		{
			while (from > 0 && isWord(from - 1))
			{
				--from;
			}

			while (to < count && isWord(to))
			{
				++to;
			}
		}

		// A wide character clicked alone still takes its right half.
		if (to < count && cells[to].WideTrailer)
		{
			++to;
		}

		return {from, to};
	}

	std::size_t TerminalColumnAt(const std::vector<Core::TerminalCell>& cells, std::size_t columns,
								 float position)
	{
		const std::size_t count = CellCount(cells, columns);

		if (!(position > 0.0f))
		{
			return 0;
		}

		const auto nearest = static_cast<std::size_t>(std::lround(position));
		const std::size_t column = std::min(nearest, count);

		// Between the two halves of a double-width character: out to the nearer of its edges.
		if (column < count && cells[column].WideTrailer && column > 0)
		{
			return position < static_cast<float>(column) ? column - 1 : column + 1;
		}

		return column;
	}

	const std::vector<Core::TerminalCell>* TerminalLineCells(const Core::TerminalScreen& screen,
															 std::int64_t line, std::size_t columns,
															 std::vector<Core::TerminalCell>& scratch)
	{
		const std::int64_t row = line - Signed(screen.GetRowsScrolledOff());

		if (row < 0)
		{
			const std::int64_t index = static_cast<std::int64_t>(screen.GetScrollbackCount()) + row;

			if (index < 0)
			{
				return nullptr;
			}

			return &screen.GetScrollbackLine(static_cast<std::size_t>(index));
		}

		if (row >= static_cast<std::int64_t>(screen.GetRows()))
		{
			return nullptr;
		}

		const std::size_t count = std::min(columns, static_cast<std::size_t>(screen.GetColumns()));

		scratch.clear();
		scratch.reserve(count);

		for (std::size_t column = 0; column < count; ++column)
		{
			scratch.push_back(screen.At(static_cast<std::uint16_t>(row), static_cast<std::uint16_t>(column)));
		}

		return &scratch;
	}

	std::size_t TerminalLineLength(const Core::TerminalScreen& screen, std::int64_t line,
								   std::size_t columns)
	{
		std::vector<Core::TerminalCell> scratch;
		const std::vector<Core::TerminalCell>* cells = TerminalLineCells(screen, line, columns, scratch);

		return cells == nullptr ? 0 : CellCount(*cells, columns);
	}

	std::size_t TerminalCellAt(const std::vector<Core::TerminalCell>& cells, std::size_t columns,
							   float position)
	{
		const std::size_t count = CellCount(cells, columns);

		if (!(position > 0.0f))
		{
			return 0;
		}

		const std::size_t column = std::min(static_cast<std::size_t>(position), count);

		// The right half of a double-width character is that character.
		if (column < count && cells[column].WideTrailer && column > 0)
		{
			return column - 1;
		}

		return column;
	}

	namespace {
		// Where TerminalPointAt and TerminalCharacterAt part: the row is found the same way, the
		// column is the nearest boundary for one and the cell under the pointer for the other.
		std::optional<Core::TextPoint> PointOnGrid(const Core::TerminalScreen& screen,
												   const TerminalGrid& grid, float x, float y,
												   bool character)
		{
			// Rows counted from the top of the screen, negative into the scrollback, as the grid
			// shows them: its top row is -ScrollOffset.
			const int firstRow = std::max(-static_cast<int>(screen.GetScrollbackCount()), -grid.ScrollOffset);
			const int lastRow =
				std::min(grid.VisibleRows - 1 - grid.ScrollOffset, static_cast<int>(screen.GetRows()) - 1);

			if (lastRow < firstRow || grid.CellHeight <= 0.0f || grid.CellWidth <= 0.0f)
			{
				return std::nullopt;
			}

			const std::uint64_t scrolled = screen.GetRowsScrolledOff();
			const int row =
				static_cast<int>(std::floor((y - grid.OriginY) / grid.CellHeight)) - grid.ScrollOffset;

			if (row < firstRow)
			{
				return Core::TextPoint{LineOfScreenRow(scrolled, firstRow), 0};
			}

			if (row > lastRow)
			{
				const std::int64_t line = LineOfScreenRow(scrolled, lastRow);
				return Core::TextPoint{line, TerminalLineLength(screen, line, grid.Columns)};
			}

			const std::int64_t line = LineOfScreenRow(scrolled, row);
			std::vector<Core::TerminalCell> scratch;
			const std::vector<Core::TerminalCell>* cells = TerminalLineCells(screen, line, grid.Columns, scratch);

			if (cells == nullptr)
			{
				return std::nullopt;
			}

			const float position = (x - grid.OriginX) / grid.CellWidth;

			return Core::TextPoint{line, character ? TerminalCellAt(*cells, grid.Columns, position)
												   : TerminalColumnAt(*cells, grid.Columns, position)};
		}
	}

	std::optional<Core::TextPoint> TerminalPointAt(const Core::TerminalScreen& screen,
												   const TerminalGrid& grid, float x, float y)
	{
		return PointOnGrid(screen, grid, x, y, false);
	}

	std::optional<Core::TextPoint> TerminalCharacterAt(const Core::TerminalScreen& screen,
													   const TerminalGrid& grid, float x, float y)
	{
		return PointOnGrid(screen, grid, x, y, true);
	}

	std::pair<Core::TextPoint, Core::TextPoint> TerminalEverything(const Core::TerminalScreen& screen,
																   std::size_t columns)
	{
		const std::uint64_t scrolled = screen.GetRowsScrolledOff();
		std::uint16_t last = screen.GetCursor().Row;

		for (auto row = static_cast<std::uint16_t>(screen.GetRows()); row > last + 1u;)
		{
			--row;

			if (!IsBlank(screen, row))
			{
				last = row;
				break;
			}
		}

		const std::int64_t lastLine = LineOfScreenRow(scrolled, last);

		return {Core::TextPoint{OldestLine(scrolled, screen.GetScrollbackCount()), 0},
				Core::TextPoint{lastLine, TerminalLineLength(screen, lastLine, columns)}};
	}

	std::string SelectedTerminalText(const Core::TextSelection& selection,
									 const Core::TerminalScreen& screen, std::size_t columns)
	{
		if (selection.IsEmpty())
		{
			return {};
		}

		const std::uint64_t scrolled = screen.GetRowsScrolledOff();

		// Only the lines that are still there, which also keeps a selection whose start left the
		// history long ago from walking every number in between.
		const std::int64_t first =
			std::max(selection.First().Line, OldestLine(scrolled, screen.GetScrollbackCount()));
		const std::int64_t last = std::min(selection.Last().Line,
										   LineOfScreenRow(scrolled, static_cast<int>(screen.GetRows()) - 1));

		std::string text;
		std::vector<Core::TerminalCell> scratch;
		bool wrote = false;

		for (std::int64_t line = first; line <= last; ++line)
		{
			const std::vector<Core::TerminalCell>* cells = TerminalLineCells(screen, line, columns, scratch);

			if (cells == nullptr)
			{
				continue;
			}

			const std::size_t count = CellCount(*cells, columns);
			const std::optional<Core::ColumnSpan> span = selection.SpanOn(line, count);

			if (!span.has_value())
			{
				continue;
			}

			const std::vector<std::size_t> bytes = TerminalCellBytes(*cells, count);
			const std::string row = TerminalRowText(*cells, count);
			std::string part = row.substr(bytes[span->From], bytes[span->To] - bytes[span->From]);

			// The grid is space-filled out to its right edge, so without this every copied line
			// would carry a tail of spaces nobody typed - and pasting it back would indent
			// whatever followed.
			while (!part.empty() && part.back() == ' ')
			{
				part.pop_back();
			}

			if (wrote)
			{
				text.push_back('\n');
			}

			wrote = true;
			text.append(part);
		}

		return text;
	}
}
