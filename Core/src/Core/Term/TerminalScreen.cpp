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
		m_ScrollBottom = static_cast<std::uint16_t>(m_Rows - 1);
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
		for (std::uint16_t step = 0; step < count; ++step)
		{
			// Only a line leaving the top of a full-height primary screen is history. A line
			// pushed out of a smaller scroll region, or out of the alternate buffer, is
			// discarded — it is a redraw artefact, not something the reader scrolled past.
			if (!m_Alternate && m_ScrollTop == 0 && m_ScrollbackLimit > 0)
			{
				m_Scrollback.push_back(m_Lines[m_ScrollTop]);

				while (m_Scrollback.size() > m_ScrollbackLimit)
				{
					m_Scrollback.pop_front();
				}
			}

			m_Lines.erase(m_Lines.begin() + m_ScrollTop);
			m_Lines.insert(m_Lines.begin() + m_ScrollBottom, std::vector<Cell>(m_Columns, Cell{}));
		}
	}

	void TerminalScreen::ScrollDown(std::uint16_t count)
	{
		for (std::uint16_t step = 0; step < count; ++step)
		{
			m_Lines.erase(m_Lines.begin() + m_ScrollBottom);
			m_Lines.insert(m_Lines.begin() + m_ScrollTop, std::vector<Cell>(m_Columns, Cell{}));
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
		if (top >= bottom || bottom >= m_Rows)
		{
			ResetScrollRegion();
			return;
		}

		m_ScrollTop = top;
		m_ScrollBottom = bottom;
		SetCursor(m_ScrollTop, 0);
	}

	void TerminalScreen::ResetScrollRegion()
	{
		m_ScrollTop = 0;
		m_ScrollBottom = static_cast<std::uint16_t>(m_Rows - 1);
	}

	void TerminalScreen::EraseInLine(EraseExtent extent)
	{
		std::vector<Cell>& line = RowAt(m_Cursor.Row);

		const std::size_t first = extent == EraseExtent::ToEnd ? m_Cursor.Column : 0;
		const std::size_t last = extent == EraseExtent::ToStart
									 ? static_cast<std::size_t>(m_Cursor.Column) + 1
									 : line.size();

		for (std::size_t column = first; column < last && column < line.size(); ++column)
		{
			ClearCell(line[column]);
		}

		m_PendingWrap = false;
	}

	void TerminalScreen::EraseInDisplay(EraseExtent extent)
	{
		const std::size_t first = extent == EraseExtent::ToEnd ? m_Cursor.Row : 0;
		const std::size_t last = extent == EraseExtent::ToStart
									 ? static_cast<std::size_t>(m_Cursor.Row) + 1
									 : m_Lines.size();

		for (std::size_t row = first; row < last && row < m_Lines.size(); ++row)
		{
			const bool partial = (row == m_Cursor.Row) && extent != EraseExtent::All;

			for (std::size_t column = 0; column < m_Lines[row].size(); ++column)
			{
				const bool before = column < m_Cursor.Column;
				const bool skip = partial
								  && ((extent == EraseExtent::ToEnd && before)
									  || (extent == EraseExtent::ToStart && !before
										  && column != m_Cursor.Column));

				if (!skip)
				{
					ClearCell(m_Lines[row][column]);
				}
			}
		}

		m_PendingWrap = false;
	}

	void TerminalScreen::InsertLines(std::uint16_t count)
	{
		if (m_Cursor.Row < m_ScrollTop || m_Cursor.Row > m_ScrollBottom)
		{
			return;
		}

		for (std::uint16_t step = 0; step < count; ++step)
		{
			m_Lines.erase(m_Lines.begin() + m_ScrollBottom);
			m_Lines.insert(m_Lines.begin() + m_Cursor.Row, std::vector<Cell>(m_Columns, Cell{}));
		}
	}

	void TerminalScreen::DeleteLines(std::uint16_t count)
	{
		if (m_Cursor.Row < m_ScrollTop || m_Cursor.Row > m_ScrollBottom)
		{
			return;
		}

		for (std::uint16_t step = 0; step < count; ++step)
		{
			m_Lines.erase(m_Lines.begin() + m_Cursor.Row);
			m_Lines.insert(m_Lines.begin() + m_ScrollBottom, std::vector<Cell>(m_Columns, Cell{}));
		}
	}

	void TerminalScreen::InsertCharacters(std::uint16_t count)
	{
		std::vector<Cell>& line = RowAt(m_Cursor.Row);

		for (std::uint16_t step = 0; step < count; ++step)
		{
			line.pop_back();
			line.insert(line.begin() + m_Cursor.Column, Cell{});
			ClearCell(line[m_Cursor.Column]);
		}
	}

	void TerminalScreen::DeleteCharacters(std::uint16_t count)
	{
		std::vector<Cell>& line = RowAt(m_Cursor.Row);

		for (std::uint16_t step = 0; step < count; ++step)
		{
			line.erase(line.begin() + m_Cursor.Column);
			line.emplace_back();
			ClearCell(line.back());
		}
	}

	void TerminalScreen::EraseCharacters(std::uint16_t count)
	{
		std::vector<Cell>& line = RowAt(m_Cursor.Row);

		for (std::uint16_t step = 0; step < count; ++step)
		{
			const std::size_t column = static_cast<std::size_t>(m_Cursor.Column) + step;

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
			m_Lines.assign(m_Rows, std::vector<Cell>(m_Columns, Cell{}));
			m_Cursor = CursorPosition{};
		}
		else
		{
			m_Lines = std::move(m_PrimaryLines);
			m_PrimaryLines.clear();
			m_Cursor = m_PrimaryCursor;
		}

		m_Alternate = alternate;
		m_PendingWrap = false;
		ResetScrollRegion();
	}

	void TerminalScreen::Resize(std::uint16_t columns, std::uint16_t rows)
	{
		m_Columns = std::max<std::uint16_t>(columns, 1);
		m_Rows = std::max<std::uint16_t>(rows, 1);

		m_Lines.resize(m_Rows, std::vector<Cell>(m_Columns, Cell{}));

		for (std::vector<Cell>& line : m_Lines)
		{
			line.resize(m_Columns, Cell{});
		}

		if (!m_PrimaryLines.empty())
		{
			m_PrimaryLines.resize(m_Rows, std::vector<Cell>(m_Columns, Cell{}));

			for (std::vector<Cell>& line : m_PrimaryLines)
			{
				line.resize(m_Columns, Cell{});
			}
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
