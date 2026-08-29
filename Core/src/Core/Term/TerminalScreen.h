#pragma once

#include "Core/Base/Base.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace Core {

	enum class ColorKind : std::uint8_t
	{
		Default,
		Palette,
		Rgb
	};

	// Kept as what the program actually asked for rather than resolved to RGB at write time:
	// the low sixteen palette entries are mapped onto the editor's colour scheme when drawn,
	// and flattening here would throw that away.
	struct Color
	{
		ColorKind Kind = ColorKind::Default;
		std::uint8_t Index = 0;
		std::uint8_t R = 0;
		std::uint8_t G = 0;
		std::uint8_t B = 0;

		static Color FromPalette(std::uint8_t index)
		{
			Color color;
			color.Kind = ColorKind::Palette;
			color.Index = index;
			return color;
		}

		static Color FromRgb(std::uint8_t r, std::uint8_t g, std::uint8_t b)
		{
			Color color;
			color.Kind = ColorKind::Rgb;
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
		Color Foreground;
		Color Background;
		CellFlags Flags = CellFlags::None;
	};

	struct Cell
	{
		char32_t Code = U' ';
		Color Foreground;
		Color Background;
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

	protected:
		std::vector<Cell>& RowAt(std::uint16_t row);
		void ClearCell(Cell& cell) const;

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
	};
}
