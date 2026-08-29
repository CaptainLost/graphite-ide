#pragma once

#include "Core/Base/Base.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace Core {

	enum class TerminalColorKind : std::uint8_t
	{
		Default,
		Palette,
		Rgb
	};

	// Kept as what the program actually asked for rather than resolved to RGB at write time:
	// the low sixteen palette entries are mapped onto the editor's colour scheme when drawn,
	// and flattening here would throw that away.
	struct TerminalColor
	{
		TerminalColorKind Kind = TerminalColorKind::Default;
		std::uint8_t Index = 0;
		std::uint8_t R = 0;
		std::uint8_t G = 0;
		std::uint8_t B = 0;

		static TerminalColor FromPalette(std::uint8_t index)
		{
			TerminalColor color;
			color.Kind = TerminalColorKind::Palette;
			color.Index = index;
			return color;
		}

		static TerminalColor FromRgb(std::uint8_t r, std::uint8_t g, std::uint8_t b)
		{
			TerminalColor color;
			color.Kind = TerminalColorKind::Rgb;
			color.R = r;
			color.G = g;
			color.B = b;
			return color;
		}
	};

	enum class CellFlags : std::uint8_t
	{
		None = 0,
		Bold = BIT(0),
		Faint = BIT(1),
		Italic = BIT(2),
		Underline = BIT(3),
		Inverse = BIT(4),
		Hidden = BIT(5)
	};

	inline CellFlags operator|(CellFlags left, CellFlags right)
	{
		return static_cast<CellFlags>(static_cast<std::uint8_t>(left)
									  | static_cast<std::uint8_t>(right));
	}

	inline CellFlags operator&(CellFlags left, CellFlags right)
	{
		return static_cast<CellFlags>(static_cast<std::uint8_t>(left)
									  & static_cast<std::uint8_t>(right));
	}

	inline CellFlags operator~(CellFlags value)
	{
		return static_cast<CellFlags>(~static_cast<std::uint8_t>(value));
	}

	inline bool HasFlag(CellFlags set, CellFlags wanted)
	{
		return (set & wanted) == wanted;
	}

	struct Pen
	{
		TerminalColor Foreground;
		TerminalColor Background;
		CellFlags Flags = CellFlags::None;
	};

	struct Cell
	{
		char32_t Code = U' ';
		TerminalColor Foreground;
		TerminalColor Background;
		CellFlags Flags = CellFlags::None;
		// The right half of a double-width character. It carries no glyph of its own; the
		// drawing code skips it, and erasing either half clears both.
		bool WideTrailer = false;
	};

	struct CursorPosition
	{
		std::uint16_t Row = 0;
		std::uint16_t Column = 0;
	};

	enum class EraseExtent : std::uint8_t
	{
		ToEnd,
		ToStart,
		All
	};

	// What a terminal currently looks like, and nothing else: no bytes are parsed here and no
	// process is owned. VtParser turns a stream into calls on this, and the panel reads it.
	class TerminalScreen
	{
	public:
		TerminalScreen(std::uint16_t columns, std::uint16_t rows,
					   std::size_t scrollbackLimit = 10000);

		void Print(char32_t code, std::uint8_t width);
		void LineFeed();
		void CarriageReturn();
		void Backspace();
		void Tab();

		void SetCursor(std::uint16_t row, std::uint16_t column);
		void MoveCursor(int rowDelta, int columnDelta);
		void SaveCursor();
		void RestoreCursor();

		void SetPen(const Pen& pen) { m_Pen = pen; }
		const Pen& GetPen() const { return m_Pen; }

		const Cell& At(std::uint16_t row, std::uint16_t column) const;

		std::uint16_t GetColumns() const { return m_Columns; }
		std::uint16_t GetRows() const { return m_Rows; }
		CursorPosition GetCursor() const { return m_Cursor; }

		void SetAutoWrap(bool wrap) { m_AutoWrap = wrap; }
		bool IsAutoWrap() const { return m_AutoWrap; }

		void SetCursorVisible(bool visible) { m_CursorVisible = visible; }
		bool IsCursorVisible() const { return m_CursorVisible; }

		void SetTitle(std::string title) { m_Title = std::move(title); }
		const std::string& GetTitle() const { return m_Title; }

		void EraseInDisplay(EraseExtent extent);
		void EraseInLine(EraseExtent extent);

		void InsertLines(std::uint16_t count);
		void DeleteLines(std::uint16_t count);
		void InsertCharacters(std::uint16_t count);
		void DeleteCharacters(std::uint16_t count);
		void EraseCharacters(std::uint16_t count);

		void ScrollUp(std::uint16_t count);
		void ScrollDown(std::uint16_t count);
		void ReverseIndex();

		void SetScrollRegion(std::uint16_t top, std::uint16_t bottom);
		void ResetScrollRegion();

		// vim and its kind draw on a second screen with no scrollback, so leaving it hands
		// back the prompt that was there before, unchanged.
		void UseAlternateBuffer(bool alternate);
		bool IsAlternateBuffer() const { return m_Alternate; }

		// Truncates and pads. Wrapped history is not reflowed — see the design document.
		void Resize(std::uint16_t columns, std::uint16_t rows);

		std::size_t GetScrollbackCount() const { return m_Scrollback.size(); }
		const std::vector<Cell>& GetScrollbackLine(std::size_t index) const
		{
			return m_Scrollback[index];
		}

	protected:
		std::vector<Cell>& RowAt(std::uint16_t row);
		void ClearCell(Cell& cell) const;

		std::uint16_t ScrollTop() const { return m_ScrollTop; }
		std::uint16_t ScrollBottom() const { return m_ScrollBottom; }

		std::vector<std::vector<Cell>> m_Lines;
		std::uint16_t m_Columns = 0;
		std::uint16_t m_Rows = 0;
		CursorPosition m_Cursor;
		CursorPosition m_SavedCursor;
		Pen m_Pen;
		// Set when a character lands in the last column. The cursor stays put and the *next*
		// printable character performs the wrap, which is what a real terminal does and what
		// makes a carriage return at that moment overwrite the current line rather than the
		// following one.
		bool m_PendingWrap = false;
		bool m_AutoWrap = true;
		bool m_CursorVisible = true;
		std::size_t m_ScrollbackLimit = 0;
		std::string m_Title;

		std::deque<std::vector<Cell>> m_Scrollback;
		std::vector<std::vector<Cell>> m_PrimaryLines;
		CursorPosition m_PrimaryCursor;
		bool m_Alternate = false;
		std::uint16_t m_ScrollTop = 0;
		std::uint16_t m_ScrollBottom = 0;
	};
}
