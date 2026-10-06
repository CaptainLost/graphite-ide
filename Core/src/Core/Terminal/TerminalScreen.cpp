#include "corepch.h"

#include "Core/Terminal/TerminalScreen.h"

#include "Core/Terminal/CharacterWidth.h"

#include <algorithm>

namespace Core {

	namespace {
		constexpr std::uint16_t kTabWidth = 8;
		constexpr std::uint16_t kSmallestDimension = 1;
		constexpr std::uint16_t kLeftmostColumn = 0;

		// A count comes from the program on the other end of the pseudo-console and only the
		// parser bounds it, at 65535, which would cost hundreds of millions of cell copies and a
		// visible stall. Repeating past the rows or columns there are changes nothing, so this
		// cap at the untrusted-input boundary must stay, like the parser's caps on its buffers.
		std::uint16_t CappedRepetitions(std::uint16_t requestedCount, std::size_t positionsToActOn)
		{
			return static_cast<std::uint16_t>(std::min<std::size_t>(requestedCount, positionsToActOn));
		}

		std::vector<TerminalCell> BlankLine(std::uint16_t columns)
		{
			return std::vector<TerminalCell>(columns, TerminalCell{});
		}

		void TruncateAndPad(std::vector<std::vector<TerminalCell>>& lines, std::uint16_t rows, std::uint16_t columns)
		{
			lines.resize(rows, BlankLine(columns));

			for (std::vector<TerminalCell>& line : lines)
			{
				line.resize(columns, TerminalCell{});
			}
		}

		bool IsCellKeptOnCursorRow(EraseExtent extent, std::size_t column, std::uint16_t cursorColumn)
		{
			const bool keptLeftOfCursor = extent == EraseExtent::ToEnd && column < cursorColumn;
			const bool keptRightOfCursor = extent == EraseExtent::ToStart && column > cursorColumn;

			return keptLeftOfCursor || keptRightOfCursor;
		}
	}

	TerminalScreen::TerminalScreen(std::uint16_t columns, std::uint16_t rows,
								   std::size_t scrollbackLimit)
		: m_Columns(std::max(columns, kSmallestDimension)),
		  m_Rows(std::max(rows, kSmallestDimension)), m_ScrollbackLimit(scrollbackLimit)
	{
		m_Lines.assign(m_Rows, BlankLine(m_Columns));
		m_ScrollBottom = LastRow();
	}

	std::vector<TerminalCell>& TerminalScreen::RowAt(std::uint16_t row)
	{
		return m_Lines[ClampToLastIndex(row, m_Lines.size())];
	}

	void TerminalScreen::ClearCell(TerminalCell& cell) const
	{
		cell = TerminalCell{.Background = m_Pen.Background};
	}

	const TerminalCell& TerminalScreen::At(std::uint16_t row, std::uint16_t column) const
	{
		const std::size_t clampedRow = ClampToLastIndex(row, m_Lines.size());
		const std::size_t clampedColumn = ClampToLastIndex(column, m_Lines[clampedRow].size());

		return m_Lines[clampedRow][clampedColumn];
	}

	void TerminalScreen::Print(char32_t code, std::uint8_t width)
	{
		if (m_PendingWrap && m_AutoWrap)
		{
			CarriageReturn();
			LineFeed();
		}

		m_PendingWrap = false;

		const std::uint8_t cellsTaken = width == 0 ? kNarrowCharacterCells : width;
		const bool overrunsRightEdge = m_Cursor.Column + cellsTaken > m_Columns;

		if (overrunsRightEdge && m_AutoWrap)
		{
			CarriageReturn();
			LineFeed();
		}

		std::vector<TerminalCell>& line = RowAt(m_Cursor.Row);

		if (m_Cursor.Column >= m_Columns)
		{
			return;
		}

		TerminalCell& printedCell = line[m_Cursor.Column];
		printedCell.Code = code;
		printedCell.Foreground = m_Pen.Foreground;
		printedCell.Background = m_Pen.Background;
		printedCell.Flags = m_Pen.Flags;
		printedCell.WideTrailer = false;

		const bool isWide = cellsTaken == kWideCharacterCells;
		const bool trailerFitsOnLine = m_Cursor.Column + 1 < m_Columns;

		if (isWide && trailerFitsOnLine)
		{
			TerminalCell& trailerCell = line[m_Cursor.Column + 1];
			trailerCell = printedCell;
			trailerCell.Code = U' ';
			trailerCell.WideTrailer = true;
		}

		const std::uint16_t columnAfterCharacter = static_cast<std::uint16_t>(m_Cursor.Column + cellsTaken);

		if (columnAfterCharacter >= m_Columns)
		{
			m_Cursor.Column = LastColumn();
			m_PendingWrap = true;
			return;
		}

		m_Cursor.Column = columnAfterCharacter;
	}

	void TerminalScreen::CarriageReturn()
	{
		m_Cursor.Column = 0;
		m_PendingWrap = false;
	}

	void TerminalScreen::LineFeed()
	{
		m_PendingWrap = false;

		if (m_Cursor.Row == m_ScrollBottom)
		{
			ScrollUp(1);
			return;
		}

		if (m_Cursor.Row + 1 < m_Rows)
		{
			m_Cursor.Row = static_cast<std::uint16_t>(m_Cursor.Row + 1);
		}
	}

	void TerminalScreen::ScrollUp(std::uint16_t count)
	{
		const std::uint16_t repetitionCount = CappedRepetitions(count, RegionHeight());

		for (std::uint16_t repetition = 0; repetition < repetitionCount; ++repetition)
		{
			// A line pushed out of a smaller scroll region or out of the alternate buffer is a
			// redraw artefact, not something the reader scrolled past; keeping it would fill the
			// history with debris each time a full-screen program redraws.
			const bool leavingLineIsHistory = !m_Alternate && IsFullHeightRegion();
			const bool scrollbackKeepsHistory = m_ScrollbackLimit > 0;

			if (leavingLineIsHistory && scrollbackKeepsHistory)
			{
				m_Scrollback.push_back(m_Lines[m_ScrollTop]);
				++m_RowsScrolledOff;

				while (m_Scrollback.size() > m_ScrollbackLimit)
				{
					m_Scrollback.pop_front();
				}
			}

			m_Lines.erase(m_Lines.begin() + m_ScrollTop);
			m_Lines.insert(m_Lines.begin() + m_ScrollBottom, BlankLine(m_Columns));
		}
	}

	void TerminalScreen::ScrollDown(std::uint16_t count)
	{
		const std::uint16_t repetitionCount = CappedRepetitions(count, RegionHeight());

		for (std::uint16_t repetition = 0; repetition < repetitionCount; ++repetition)
		{
			m_Lines.erase(m_Lines.begin() + m_ScrollBottom);
			m_Lines.insert(m_Lines.begin() + m_ScrollTop, BlankLine(m_Columns));
		}
	}

	void TerminalScreen::ReverseIndex()
	{
		m_PendingWrap = false;

		if (m_Cursor.Row == m_ScrollTop)
		{
			ScrollDown(1);
			return;
		}

		if (m_Cursor.Row > 0)
		{
			m_Cursor.Row = static_cast<std::uint16_t>(m_Cursor.Row - 1);
		}
	}

	void TerminalScreen::SetScrollRegion(std::uint16_t top, std::uint16_t bottom)
	{
		const bool regionHasFewerThanTwoRows = top >= bottom;
		const bool regionLeavesScreen = bottom >= m_Rows;

		if (regionHasFewerThanTwoRows || regionLeavesScreen)
		{
			ResetScrollRegion();
			return;
		}

		m_ScrollTop = top;
		m_ScrollBottom = bottom;
		SetCursor(m_ScrollTop, kLeftmostColumn);
	}

	void TerminalScreen::ResetScrollRegion()
	{
		m_ScrollTop = 0;
		m_ScrollBottom = LastRow();
	}

	void TerminalScreen::EraseInLine(EraseExtent extent)
	{
		std::vector<TerminalCell>& line = RowAt(m_Cursor.Row);

		const std::size_t columnAfterCursor = static_cast<std::size_t>(m_Cursor.Column) + 1;
		const std::size_t firstColumn = extent == EraseExtent::ToEnd ? m_Cursor.Column : 0;
		const std::size_t endColumn = extent == EraseExtent::ToStart ? columnAfterCursor : line.size();

		for (std::size_t column = firstColumn; column < endColumn && column < line.size(); ++column)
		{
			ClearCell(line[column]);
		}

		m_PendingWrap = false;
	}

	void TerminalScreen::EraseInDisplay(EraseExtent extent)
	{
		const std::size_t rowAfterCursor = static_cast<std::size_t>(m_Cursor.Row) + 1;
		const std::size_t firstRow = extent == EraseExtent::ToEnd ? m_Cursor.Row : 0;
		const std::size_t endRow = extent == EraseExtent::ToStart ? rowAfterCursor : m_Lines.size();

		for (std::size_t row = firstRow; row < endRow && row < m_Lines.size(); ++row)
		{
			const bool erasesPartOfRow = row == m_Cursor.Row && extent != EraseExtent::All;

			for (std::size_t column = 0; column < m_Lines[row].size(); ++column)
			{
				const bool keepsCell = erasesPartOfRow && IsCellKeptOnCursorRow(extent, column, m_Cursor.Column);

				if (!keepsCell)
				{
					ClearCell(m_Lines[row][column]);
				}
			}
		}

		m_PendingWrap = false;
	}

	void TerminalScreen::InsertLines(std::uint16_t count)
	{
		if (!IsCursorInsideScrollRegion())
		{
			return;
		}

		const std::uint16_t repetitionCount = CappedRepetitions(count, RowsFromCursorToScrollBottom());

		for (std::uint16_t repetition = 0; repetition < repetitionCount; ++repetition)
		{
			m_Lines.erase(m_Lines.begin() + m_ScrollBottom);
			m_Lines.insert(m_Lines.begin() + m_Cursor.Row, BlankLine(m_Columns));
		}
	}

	void TerminalScreen::DeleteLines(std::uint16_t count)
	{
		if (!IsCursorInsideScrollRegion())
		{
			return;
		}

		const std::uint16_t repetitionCount = CappedRepetitions(count, RowsFromCursorToScrollBottom());

		for (std::uint16_t repetition = 0; repetition < repetitionCount; ++repetition)
		{
			m_Lines.erase(m_Lines.begin() + m_Cursor.Row);
			m_Lines.insert(m_Lines.begin() + m_ScrollBottom, BlankLine(m_Columns));
		}
	}

	void TerminalScreen::InsertCharacters(std::uint16_t count)
	{
		std::vector<TerminalCell>& line = RowAt(m_Cursor.Row);

		const std::size_t cellsFromCursorToLineEnd = line.size() - m_Cursor.Column;
		const std::uint16_t repetitionCount = CappedRepetitions(count, cellsFromCursorToLineEnd);

		for (std::uint16_t repetition = 0; repetition < repetitionCount; ++repetition)
		{
			line.pop_back();
			line.insert(line.begin() + m_Cursor.Column, TerminalCell{});
			ClearCell(line[m_Cursor.Column]);
		}
	}

	void TerminalScreen::DeleteCharacters(std::uint16_t count)
	{
		std::vector<TerminalCell>& line = RowAt(m_Cursor.Row);

		const std::size_t cellsFromCursorToLineEnd = line.size() - m_Cursor.Column;
		const std::uint16_t repetitionCount = CappedRepetitions(count, cellsFromCursorToLineEnd);

		for (std::uint16_t repetition = 0; repetition < repetitionCount; ++repetition)
		{
			line.erase(line.begin() + m_Cursor.Column);
			line.emplace_back();
			ClearCell(line.back());
		}
	}

	void TerminalScreen::EraseCharacters(std::uint16_t count)
	{
		std::vector<TerminalCell>& line = RowAt(m_Cursor.Row);

		for (std::uint16_t offsetFromCursor = 0; offsetFromCursor < count; ++offsetFromCursor)
		{
			const std::size_t column = static_cast<std::size_t>(m_Cursor.Column) + offsetFromCursor;

			if (column >= line.size())
			{
				break;
			}

			ClearCell(line[column]);
		}
	}

	void TerminalScreen::UseAlternateBuffer(bool alternate)
	{
		if (alternate == m_Alternate)
		{
			return;
		}

		if (alternate)
		{
			m_PrimaryLines = std::move(m_Lines);
			m_PrimaryCursor = m_Cursor;
			m_Lines.assign(m_Rows, BlankLine(m_Columns));
			m_Cursor = CursorPosition{};
		}
		else
		{
			m_Lines = std::move(m_PrimaryLines);
			m_PrimaryLines.clear();
			// Through SetCursor, like every other restore: a resize while the alternate buffer
			// was up can leave the saved row off the screen, below the scroll bottom, where a
			// line feed neither scrolls nor advances and every later line overwrites the bottom row.
			SetCursor(m_PrimaryCursor.Row, m_PrimaryCursor.Column);
		}

		m_Alternate = alternate;
		m_PendingWrap = false;
		ResetScrollRegion();
	}

	void TerminalScreen::Resize(std::uint16_t columns, std::uint16_t rows)
	{
		m_Columns = std::max(columns, kSmallestDimension);
		m_Rows = std::max(rows, kSmallestDimension);

		TruncateAndPad(m_Lines, m_Rows, m_Columns);

		if (!m_PrimaryLines.empty())
		{
			TruncateAndPad(m_PrimaryLines, m_Rows, m_Columns);
		}

		ResetScrollRegion();
		SetCursor(m_Cursor.Row, m_Cursor.Column);
	}

	void TerminalScreen::Backspace()
	{
		m_PendingWrap = false;

		if (m_Cursor.Column > 0)
		{
			m_Cursor.Column = static_cast<std::uint16_t>(m_Cursor.Column - 1);
		}
	}

	void TerminalScreen::Tab()
	{
		m_PendingWrap = false;

		const std::uint16_t nextTabStop =
			static_cast<std::uint16_t>((m_Cursor.Column / kTabWidth + 1) * kTabWidth);

		m_Cursor.Column = std::min(nextTabStop, LastColumn());
	}

	void TerminalScreen::SetCursor(std::uint16_t row, std::uint16_t column)
	{
		m_Cursor.Row = std::min(row, LastRow());
		m_Cursor.Column = std::min(column, LastColumn());
		m_PendingWrap = false;
	}

	void TerminalScreen::MoveCursor(int rowDelta, int columnDelta)
	{
		const int targetRow = std::clamp(static_cast<int>(m_Cursor.Row) + rowDelta, 0, m_Rows - 1);
		const int targetColumn =
			std::clamp(static_cast<int>(m_Cursor.Column) + columnDelta, 0, m_Columns - 1);

		SetCursor(static_cast<std::uint16_t>(targetRow), static_cast<std::uint16_t>(targetColumn));
	}

	void TerminalScreen::SaveCursor()
	{
		m_SavedCursor = m_Cursor;
	}

	void TerminalScreen::RestoreCursor()
	{
		SetCursor(m_SavedCursor.Row, m_SavedCursor.Column);
	}
}
