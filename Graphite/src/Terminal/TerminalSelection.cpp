#include "Terminal/TerminalSelection.h"

#include "Core/Text/Utf8.h"
#include "Core/Text/WordBounds.h"

#include <algorithm>
#include <cmath>

namespace Graphite {

	namespace {
		constexpr std::size_t LineStartColumn = 0;
		constexpr std::size_t NoBytesYet = 0;
		constexpr char LineBreak = '\n';
		constexpr char TypedSpace = ' ';

		enum class ColumnRule
		{
			NearestBoundary,
			CellUnderPointer,
		};

		std::int64_t AsSigned(std::uint64_t value)
		{
			return static_cast<std::int64_t>(value);
		}

		std::int64_t OldestLine(std::uint64_t rowsScrolledOff, std::size_t scrollbackCount)
		{
			return AsSigned(rowsScrolledOff) - static_cast<std::int64_t>(scrollbackCount);
		}

		std::size_t VisibleCellCount(const std::vector<Core::TerminalCell>& cells, std::size_t columns)
		{
			return std::min(columns, cells.size());
		}

		bool IsSecondHalfOfWideCharacter(const std::vector<Core::TerminalCell>& cells, std::size_t column)
		{
			return cells[column].WideTrailer && column > 0;
		}

		char32_t CodePointAt(const std::vector<Core::TerminalCell>& cells, std::size_t column)
		{
			if (IsSecondHalfOfWideCharacter(cells, column))
			{
				return cells[column - 1].Code;
			}

			return cells[column].Code;
		}

		bool IsPathWordCell(const std::vector<Core::TerminalCell>& cells, std::size_t column)
		{
			return Core::IsWordCodePoint(CodePointAt(cells, column), Core::WordKind::Path);
		}

		bool IsBlankScreenRow(const Core::TerminalScreen& screen, std::uint16_t row)
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

		std::string WithoutTrailingSpaces(std::string text)
		{
			while (!text.empty() && text.back() == TypedSpace)
			{
				text.pop_back();
			}

			return text;
		}
	}

	std::int64_t LineOfScrollbackRow(std::uint64_t rowsScrolledOff, std::size_t scrollbackCount,
									 std::size_t index)
	{
		return OldestLine(rowsScrolledOff, scrollbackCount) + static_cast<std::int64_t>(index);
	}

	std::int64_t LineOfScreenRow(std::uint64_t rowsScrolledOff, int screenRow)
	{
		return AsSigned(rowsScrolledOff) + screenRow;
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

		if (selection.IsEmpty())
		{
			return selection;
		}

		const bool startIsHeld = selection.First().Line >= oldest;
		const bool endIsGone = selection.Last().Line < oldest;

		if (startIsHeld || endIsGone)
		{
			return selection;
		}

		const Core::TextPoint oldestLineStart{oldest, LineStartColumn};
		const bool pressedIsFirst = selection.Pressed() == selection.First();

		Core::TextSelection clampedSelection;
		clampedSelection.Select(pressedIsFirst ? oldestLineStart : selection.Pressed(),
								pressedIsFirst ? selection.Last() : oldestLineStart);
		return clampedSelection;
	}

	std::vector<std::size_t> TerminalCellBytes(const std::vector<Core::TerminalCell>& cells, std::size_t columns)
	{
		std::vector<std::size_t> cellByteOffsets(columns + 1, NoBytesYet);
		const std::size_t visibleCount = VisibleCellCount(cells, columns);
		std::string encodedCell;
		std::size_t byteOffset = NoBytesYet;

		for (std::size_t column = 0; column < columns; ++column)
		{
			cellByteOffsets[column] = byteOffset;

			if (column < visibleCount && !cells[column].WideTrailer)
			{
				encodedCell.clear();
				Core::AppendUtf8(encodedCell, cells[column].Code);
				byteOffset += encodedCell.size();
			}
		}

		cellByteOffsets[columns] = byteOffset;
		return cellByteOffsets;
	}

	std::string TerminalRowText(const std::vector<Core::TerminalCell>& cells, std::size_t columns)
	{
		const std::size_t visibleCount = VisibleCellCount(cells, columns);
		std::string text;

		for (std::size_t column = 0; column < visibleCount; ++column)
		{
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
		const std::size_t visibleCount = VisibleCellCount(cells, columns);

		if (column >= visibleCount)
		{
			return {visibleCount, visibleCount};
		}

		if (IsSecondHalfOfWideCharacter(cells, column))
		{
			--column;
		}

		std::size_t spanStart = column;
		std::size_t spanEnd = column + 1;

		if (IsPathWordCell(cells, column))
		{
			while (spanStart > 0 && IsPathWordCell(cells, spanStart - 1))
			{
				--spanStart;
			}

			while (spanEnd < visibleCount && IsPathWordCell(cells, spanEnd))
			{
				++spanEnd;
			}
		}

		const bool endsBeforeSecondHalf = spanEnd < visibleCount && cells[spanEnd].WideTrailer;

		if (endsBeforeSecondHalf)
		{
			++spanEnd;
		}

		return {spanStart, spanEnd};
	}

	std::size_t TerminalColumnAt(const std::vector<Core::TerminalCell>& cells, std::size_t columns,
								 float position)
	{
		const std::size_t visibleCount = VisibleCellCount(cells, columns);

		// WARNING: The negated comparison is deliberate: it also sends a NaN position to column 0.
		if (!(position > 0.0f))
		{
			return 0;
		}

		const std::size_t nearestBoundary = static_cast<std::size_t>(std::lround(position));
		const std::size_t column = std::min(nearestBoundary, visibleCount);

		if (column < visibleCount && IsSecondHalfOfWideCharacter(cells, column))
		{
			const bool nearerToLeftEdge = position < static_cast<float>(column);
			return nearerToLeftEdge ? column - 1 : column + 1;
		}

		return column;
	}

	const std::vector<Core::TerminalCell>* TerminalLineCells(const Core::TerminalScreen& screen,
															 std::int64_t line, std::size_t columns,
															 std::vector<Core::TerminalCell>& rowCopy)
	{
		const std::int64_t screenRow = line - AsSigned(screen.GetRowsScrolledOff());

		if (screenRow < 0)
		{
			const std::int64_t scrollbackIndex = static_cast<std::int64_t>(screen.GetScrollbackCount()) + screenRow;

			if (scrollbackIndex < 0)
			{
				return nullptr;
			}

			return &screen.GetScrollbackLine(static_cast<std::size_t>(scrollbackIndex));
		}

		if (screenRow >= static_cast<std::int64_t>(screen.GetRows()))
		{
			return nullptr;
		}

		const std::size_t copiedCount = std::min(columns, static_cast<std::size_t>(screen.GetColumns()));

		rowCopy.clear();
		rowCopy.reserve(copiedCount);

		for (std::size_t column = 0; column < copiedCount; ++column)
		{
			rowCopy.push_back(
				screen.At(static_cast<std::uint16_t>(screenRow), static_cast<std::uint16_t>(column)));
		}

		return &rowCopy;
	}

	std::size_t TerminalLineLength(const Core::TerminalScreen& screen, std::int64_t line,
								   std::size_t columns)
	{
		std::vector<Core::TerminalCell> rowCopy;
		const std::vector<Core::TerminalCell>* cells = TerminalLineCells(screen, line, columns, rowCopy);

		return cells == nullptr ? 0 : VisibleCellCount(*cells, columns);
	}

	std::size_t TerminalCellAt(const std::vector<Core::TerminalCell>& cells, std::size_t columns,
							   float position)
	{
		const std::size_t visibleCount = VisibleCellCount(cells, columns);

		// WARNING: The negated comparison is deliberate: it also sends a NaN position to column 0.
		if (!(position > 0.0f))
		{
			return 0;
		}

		const std::size_t column = std::min(static_cast<std::size_t>(position), visibleCount);

		if (column < visibleCount && IsSecondHalfOfWideCharacter(cells, column))
		{
			return column - 1;
		}

		return column;
	}

	namespace {
		std::optional<Core::TextPoint> PointOnGrid(const Core::TerminalScreen& screen,
												   const TerminalGrid& grid, float x, float y,
												   ColumnRule columnRule)
		{
			const int oldestShownScreenRow =
				std::max(-static_cast<int>(screen.GetScrollbackCount()), -grid.ScrollOffset);
			const int newestShownScreenRow =
				std::min(grid.VisibleRows - 1 - grid.ScrollOffset, static_cast<int>(screen.GetRows()) - 1);

			if (newestShownScreenRow < oldestShownScreenRow || grid.CellHeight <= 0.0f ||
				grid.CellWidth <= 0.0f)
			{
				return std::nullopt;
			}

			const std::uint64_t rowsScrolledOff = screen.GetRowsScrolledOff();
			const int screenRow =
				static_cast<int>(std::floor((y - grid.OriginY) / grid.CellHeight)) - grid.ScrollOffset;

			if (screenRow < oldestShownScreenRow)
			{
				return Core::TextPoint{LineOfScreenRow(rowsScrolledOff, oldestShownScreenRow),
									   LineStartColumn};
			}

			if (screenRow > newestShownScreenRow)
			{
				const std::int64_t newestShownLine = LineOfScreenRow(rowsScrolledOff, newestShownScreenRow);
				return Core::TextPoint{newestShownLine,
									   TerminalLineLength(screen, newestShownLine, grid.Columns)};
			}

			const std::int64_t line = LineOfScreenRow(rowsScrolledOff, screenRow);
			std::vector<Core::TerminalCell> rowCopy;
			const std::vector<Core::TerminalCell>* cells =
				TerminalLineCells(screen, line, grid.Columns, rowCopy);

			if (cells == nullptr)
			{
				return std::nullopt;
			}

			const float position = (x - grid.OriginX) / grid.CellWidth;
			const std::size_t column = columnRule == ColumnRule::CellUnderPointer
										   ? TerminalCellAt(*cells, grid.Columns, position)
										   : TerminalColumnAt(*cells, grid.Columns, position);

			return Core::TextPoint{line, column};
		}
	}

	std::optional<Core::TextPoint> TerminalPointAt(const Core::TerminalScreen& screen,
												   const TerminalGrid& grid, float x, float y)
	{
		return PointOnGrid(screen, grid, x, y, ColumnRule::NearestBoundary);
	}

	std::optional<Core::TextPoint> TerminalCharacterAt(const Core::TerminalScreen& screen,
													   const TerminalGrid& grid, float x, float y)
	{
		return PointOnGrid(screen, grid, x, y, ColumnRule::CellUnderPointer);
	}

	std::pair<Core::TextPoint, Core::TextPoint> TerminalEverything(const Core::TerminalScreen& screen,
																   std::size_t columns)
	{
		const std::uint64_t rowsScrolledOff = screen.GetRowsScrolledOff();
		std::uint16_t lastUsedRow = screen.GetCursor().Row;

		for (std::uint16_t row = static_cast<std::uint16_t>(screen.GetRows()); row > lastUsedRow + 1u;)
		{
			--row;

			if (!IsBlankScreenRow(screen, row))
			{
				lastUsedRow = row;
				break;
			}
		}

		const std::int64_t lastLine = LineOfScreenRow(rowsScrolledOff, lastUsedRow);
		const Core::TextPoint everythingStart{OldestLine(rowsScrolledOff, screen.GetScrollbackCount()),
											  LineStartColumn};
		const Core::TextPoint everythingEnd{lastLine, TerminalLineLength(screen, lastLine, columns)};

		return {everythingStart, everythingEnd};
	}

	std::string SelectedTerminalText(const Core::TextSelection& selection,
									 const Core::TerminalScreen& screen, std::size_t columns)
	{
		if (selection.IsEmpty())
		{
			return {};
		}

		const std::uint64_t rowsScrolledOff = screen.GetRowsScrolledOff();
		const int lastScreenRow = static_cast<int>(screen.GetRows()) - 1;

		const std::int64_t firstLine = std::max(
			selection.First().Line, OldestLine(rowsScrolledOff, screen.GetScrollbackCount()));
		const std::int64_t lastLine =
			std::min(selection.Last().Line, LineOfScreenRow(rowsScrolledOff, lastScreenRow));

		std::string text;
		std::vector<Core::TerminalCell> rowCopy;
		bool wroteALine = false;

		for (std::int64_t line = firstLine; line <= lastLine; ++line)
		{
			const std::vector<Core::TerminalCell>* cells = TerminalLineCells(screen, line, columns, rowCopy);

			if (cells == nullptr)
			{
				continue;
			}

			const std::size_t visibleCount = VisibleCellCount(*cells, columns);
			const std::optional<Core::ColumnSpan> span = selection.SpanOn(line, visibleCount);

			if (!span.has_value())
			{
				continue;
			}

			const std::vector<std::size_t> cellByteOffsets = TerminalCellBytes(*cells, visibleCount);
			const std::string rowText = TerminalRowText(*cells, visibleCount);
			const std::size_t spanStartByte = cellByteOffsets[span->From];
			const std::size_t spanByteCount = cellByteOffsets[span->To] - spanStartByte;
			const std::string selectedPart =
				WithoutTrailingSpaces(rowText.substr(spanStartByte, spanByteCount));

			if (wroteALine)
			{
				text.push_back(LineBreak);
			}

			wroteALine = true;
			text.append(selectedPart);
		}

		return text;
	}
}
