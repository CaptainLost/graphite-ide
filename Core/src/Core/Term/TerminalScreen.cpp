#include "corepch.h"

#include "Core/Term/TerminalScreen.h"

#include <algorithm>

namespace Core {

	namespace {
		constexpr std::uint16_t TabWidth = 8;
	}

	TerminalScreen::TerminalScreen(std::uint16_t columns, std::uint16_t rows,
								   std::size_t scrollbackLimit)
		: m_Columns(std::max<std::uint16_t>(columns, 1)),
		  m_Rows(std::max<std::uint16_t>(rows, 1)), m_ScrollbackLimit(scrollbackLimit)
	{
		m_Lines.assign(m_Rows, std::vector<Cell>(m_Columns, Cell{}));
	}

	std::vector<Cell>& TerminalScreen::RowAt(std::uint16_t row)
	{
		return m_Lines[std::min<std::size_t>(row, m_Lines.size() - 1)];
	}

	void TerminalScreen::ClearCell(Cell& cell) const
	{
		cell = Cell{};
		cell.Background = m_Pen.Background;
	}

	const Cell& TerminalScreen::At(std::uint16_t row, std::uint16_t column) const
	{
		const std::size_t clampedRow = std::min<std::size_t>(row, m_Lines.size() - 1);
		const std::size_t clampedColumn =
			std::min<std::size_t>(column, m_Lines[clampedRow].size() - 1);

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

		const std::uint8_t cells = width == 0 ? 1 : width;

		// A double-width character that does not fit against the right edge moves to the next
		// line whole rather than being split across the boundary.
		if (m_Cursor.Column + cells > m_Columns && m_AutoWrap)
		{
			CarriageReturn();
			LineFeed();
		}

		std::vector<Cell>& line = RowAt(m_Cursor.Row);

		if (m_Cursor.Column >= m_Columns)
		{
			return;
		}

		Cell& target = line[m_Cursor.Column];
		target.Code = code;
		target.Foreground = m_Pen.Foreground;
		target.Background = m_Pen.Background;
		target.Flags = m_Pen.Flags;
		target.WideTrailer = false;

		if (cells == 2 && m_Cursor.Column + 1 < m_Columns)
		{
			Cell& trailer = line[m_Cursor.Column + 1];
			trailer = target;
			trailer.Code = U' ';
			trailer.WideTrailer = true;
		}

		const std::uint16_t advanced = static_cast<std::uint16_t>(m_Cursor.Column + cells);

		if (advanced >= m_Columns)
		{
			m_Cursor.Column = static_cast<std::uint16_t>(m_Columns - 1);
			m_PendingWrap = true;
			return;
		}

		m_Cursor.Column = advanced;
	}

	void TerminalScreen::CarriageReturn()
	{
		m_Cursor.Column = 0;
		m_PendingWrap = false;
	}

	void TerminalScreen::LineFeed()
	{
		m_PendingWrap = false;

		if (m_Cursor.Row + 1 < m_Rows)
		{
			m_Cursor.Row = static_cast<std::uint16_t>(m_Cursor.Row + 1);
			return;
		}

		// At the bottom the screen scrolls instead. Task 4 replaces this with the scroll
		// region-aware version; until then the whole screen moves.
		m_Lines.erase(m_Lines.begin());
		m_Lines.emplace_back(m_Columns, Cell{});
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

		const std::uint16_t next =
			static_cast<std::uint16_t>((m_Cursor.Column / TabWidth + 1) * TabWidth);

		m_Cursor.Column = std::min<std::uint16_t>(next, static_cast<std::uint16_t>(m_Columns - 1));
	}

	void TerminalScreen::SetCursor(std::uint16_t row, std::uint16_t column)
	{
		m_Cursor.Row = std::min<std::uint16_t>(row, static_cast<std::uint16_t>(m_Rows - 1));
		m_Cursor.Column =
			std::min<std::uint16_t>(column, static_cast<std::uint16_t>(m_Columns - 1));
		m_PendingWrap = false;
	}

	void TerminalScreen::MoveCursor(int rowDelta, int columnDelta)
	{
		const int row = std::clamp(static_cast<int>(m_Cursor.Row) + rowDelta, 0, m_Rows - 1);
		const int column =
			std::clamp(static_cast<int>(m_Cursor.Column) + columnDelta, 0, m_Columns - 1);

		SetCursor(static_cast<std::uint16_t>(row), static_cast<std::uint16_t>(column));
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
