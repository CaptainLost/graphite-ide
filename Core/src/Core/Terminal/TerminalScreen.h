#pragma once

#include "Core/Base/Base.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

namespace Core {

	enum class TerminalColorKind : std::uint8_t
	{
		Default,
		Palette,
		Rgb
	};

	// Kept as the program asked for it rather than resolved to RGB: only a default colour takes
	// the panel's own foreground and background, so flattening it here would make it
	// indistinguishable from a named one.
	struct TerminalColor
	{
		TerminalColorKind Kind = TerminalColorKind::Default;
		std::uint8_t Index = 0;
		std::uint8_t R = 0;
		std::uint8_t G = 0;
		std::uint8_t B = 0;

		static TerminalColor FromPalette(std::uint8_t index)
		{
			return TerminalColor{.Kind = TerminalColorKind::Palette, .Index = index};
		}

		static TerminalColor FromRgb(std::uint8_t red, std::uint8_t green, std::uint8_t blue)
		{
			return TerminalColor{.Kind = TerminalColorKind::Rgb, .R = red, .G = green, .B = blue};
		}
	};

	enum class CellFlags : std::uint8_t
	{
		None = 0,
		Bold = 1 << 0,
		Faint = 1 << 1,
		Italic = 1 << 2,
		Underline = 1 << 3,
		Inverse = 1 << 4,
		Hidden = 1 << 5
	};

	inline CellFlags operator|(CellFlags left, CellFlags right)
	{
		return static_cast<CellFlags>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
	}

	inline CellFlags operator&(CellFlags left, CellFlags right)
	{
		return static_cast<CellFlags>(static_cast<std::uint8_t>(left) & static_cast<std::uint8_t>(right));
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

	struct TerminalCell
	{
		char32_t Code = U' ';
		TerminalColor Foreground;
		TerminalColor Background;
		CellFlags Flags = CellFlags::None;
		// The right half of a double-width character, which carries no glyph of its own.
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

	class TerminalScreen
	{
	public:
		static constexpr std::size_t kDefaultScrollbackLimit = 10000;

		TerminalScreen(std::uint16_t columns, std::uint16_t rows,
					   std::size_t scrollbackLimit = kDefaultScrollbackLimit);

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

		const TerminalCell& At(std::uint16_t row, std::uint16_t column) const;

		std::uint16_t GetColumns() const { return m_Columns; }
		std::uint16_t GetRows() const { return m_Rows; }
		CursorPosition GetCursor() const { return m_Cursor; }

		void SetAutoWrap(bool wrap) { m_AutoWrap = wrap; }
		bool IsAutoWrap() const { return m_AutoWrap; }

		void SetCursorVisible(bool visible) { m_CursorVisible = visible; }
		bool IsCursorVisible() const { return m_CursorVisible; }

		// A shell asks for this so it can tell a paste from typing and not run each line the
		// paste contains.
		void SetBracketedPaste(bool wrapPastesInMarkers) { m_BracketedPaste = wrapPastesInMarkers; }
		bool IsBracketedPaste() const { return m_BracketedPaste; }

		enum class MouseMode : std::uint8_t
		{
			Clicks = 1 << 0,
			Drags = 1 << 1,
			Motion = 1 << 2
		};

		void SetMouseMode(MouseMode mode, bool enabled)
		{
			const auto modeBit = static_cast<std::uint8_t>(mode);
			m_MouseModes = static_cast<std::uint8_t>(enabled ? m_MouseModes | modeBit : m_MouseModes & ~modeBit);
		}

		// A program that turns on any of the mouse modes is asking for the wheel as well, and
		// draws its own scrolling, usually on the alternate buffer where the view has no
		// scrollback to move through.
		bool IsMouseReporting() const { return m_MouseModes != 0; }

		void SetSgrMouse(bool sgrDecimalForm) { m_SgrMouse = sgrDecimalForm; }
		bool IsSgrMouse() const { return m_SgrMouse; }

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

		void UseAlternateBuffer(bool alternate);
		bool IsAlternateBuffer() const { return m_Alternate; }

		void Resize(std::uint16_t columns, std::uint16_t rows);

		std::size_t GetScrollbackCount() const { return m_Scrollback.size(); }

		// Counts only rows that entered the scrollback, and never decreases, unlike GetScrollbackCount:
		// a view numbers rows from it, so a row keeps its number while output scrolls it up the
		// screen and into the history.
		std::uint64_t GetRowsScrolledOff() const { return m_RowsScrolledOff; }

		// Clearing a terminal needs this as well as erasing the grid, or what the reader wanted
		// gone stays one wheel notch away.
		void ClearScrollback() { m_Scrollback.clear(); }

		// Clamped like At, because a scroll offset can name a line the scrollback has since
		// dropped, and the accessors must agree that it is a nearby line rather than a crash.
		const std::vector<TerminalCell>& GetScrollbackLine(std::size_t index) const
		{
			CORE_ASSERT(!m_Scrollback.empty(), "TerminalScreen::GetScrollbackLine called on an empty scrollback");
			return m_Scrollback[ClampToLastIndex(index, m_Scrollback.size())];
		}

	private:
		std::vector<TerminalCell>& RowAt(std::uint16_t row);
		void ClearCell(TerminalCell& cell) const;

		static std::size_t ClampToLastIndex(std::size_t index, std::size_t count)
		{
			return std::min(index, count - 1);
		}

		std::uint16_t LastRow() const { return static_cast<std::uint16_t>(m_Rows - 1); }
		std::uint16_t LastColumn() const { return static_cast<std::uint16_t>(m_Columns - 1); }

		std::size_t RegionHeight() const
		{
			return static_cast<std::size_t>(m_ScrollBottom - m_ScrollTop) + 1;
		}

		bool IsFullHeightRegion() const
		{
			return m_ScrollTop == 0 && m_ScrollBottom + 1 == m_Rows;
		}

		bool IsCursorInsideScrollRegion() const
		{
			return m_Cursor.Row >= m_ScrollTop && m_Cursor.Row <= m_ScrollBottom;
		}

		std::size_t RowsFromCursorToScrollBottom() const
		{
			return static_cast<std::size_t>(m_ScrollBottom - m_Cursor.Row) + 1;
		}

		std::vector<std::vector<TerminalCell>> m_Lines;
		std::uint16_t m_Columns = 0;
		std::uint16_t m_Rows = 0;
		CursorPosition m_Cursor;
		CursorPosition m_SavedCursor;
		Pen m_Pen;
		// A carriage return while this is set overwrites the current line rather than the next
		// one, as a real terminal does.
		bool m_PendingWrap = false;
		bool m_AutoWrap = true;
		bool m_CursorVisible = true;
		bool m_BracketedPaste = false;
		std::uint8_t m_MouseModes = 0;
		bool m_SgrMouse = false;
		std::size_t m_ScrollbackLimit = 0;
		std::string m_Title;

		std::deque<std::vector<TerminalCell>> m_Scrollback;
		std::uint64_t m_RowsScrolledOff = 0;
		std::vector<std::vector<TerminalCell>> m_PrimaryLines;
		CursorPosition m_PrimaryCursor;
		bool m_Alternate = false;
		std::uint16_t m_ScrollTop = 0;
		std::uint16_t m_ScrollBottom = 0;
	};
}
