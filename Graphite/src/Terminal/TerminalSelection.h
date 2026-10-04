#pragma once

#include "Core/Terminal/TerminalScreen.h"
#include "Core/Text/TextSelection.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Graphite {

	// The terminal's side of the shared selection. A line is the number of rows the screen had
	// pushed into its scrollback (Core::TerminalScreen::GetRowsScrolledOff) at the moment plus the
	// row's place, so the same text keeps the same number while output scrolls it up the screen
	// and into the history. A column is a cell index; a double-width character's trailer is a
	// column of its own that carries no text.
	//
	// Only rows pushed into the scrollback move the count, so rows scrolling inside a DECSTBM
	// region, or on a screen whose scrollback limit is 0, keep their numbers while their text
	// moves under them.

	// The line of the scrollback row at `index` (0 = oldest kept) and of the screen row
	// `screenRow`. Both come to rowsScrolledOff + the row's place counted from the top of the
	// screen, negative into the history.
	std::int64_t LineOfScrollbackRow(std::uint64_t rowsScrolledOff, std::size_t scrollbackCount,
									 std::size_t index);
	std::int64_t LineOfScreenRow(std::uint64_t rowsScrolledOff, int screenRow);

	// True when the selection starts on a line the scrollback no longer holds: its text has gone,
	// so what is left of it would name rows that are now something else, or nothing. An empty
	// selection never left anything.
	bool SelectionLeftHistory(const Core::TextSelection& selection, std::uint64_t rowsScrolledOff,
							  std::size_t scrollbackCount);

	// The byte offset in TerminalRowText at which each of the first `columns` cells starts, plus
	// one past the end: columns + 1 entries. A trailer starts where its leader ends, and cells the
	// row does not have all start at the end of the text.
	std::vector<std::size_t> TerminalCellBytes(const std::vector<Core::Cell>& cells, std::size_t columns);

	// The first `columns` cells as UTF-8, a double-width character once.
	std::string TerminalRowText(const std::vector<Core::Cell>& cells, std::size_t columns);

	// The span of cells around `column` that a double-click selects: the run of path-word
	// characters (Core::WordKind::Path, so `src/main.cpp:42` is one word), or the one character
	// clicked when it is not a word character. A trailer counts with its leader, so a span never
	// splits a double-width character. Past the end of the row the span is empty.
	Core::ColumnSpan TerminalWordAt(const std::vector<Core::Cell>& cells, std::size_t columns,
									std::size_t column);

	// The cell boundary nearest to `position`, counted in cells from the row's left edge and
	// clamped to the row. A boundary inside a double-width character moves to whichever of its
	// edges is nearer.
	std::size_t TerminalColumnAt(const std::vector<Core::Cell>& cells, std::size_t columns,
								 float position);

	// The cell under `position` (truncated, not rounded), clamped to the row; the right half of a
	// double-width character names its leader. Where a double-click looks for its word.
	std::size_t TerminalCellAt(const std::vector<Core::Cell>& cells, std::size_t columns,
							   float position);

	// The cells of `line`, or nullptr when the line has left the scrollback or lies below the
	// screen. Screen rows are copied into `scratch`, at most `columns` cells of them.
	const std::vector<Core::Cell>* TerminalLineCells(const Core::TerminalScreen& screen,
													 std::int64_t line, std::size_t columns,
													 std::vector<Core::Cell>& scratch);

	// How many cells of `line` the view shows: zero for a line that is not there.
	std::size_t TerminalLineLength(const Core::TerminalScreen& screen, std::int64_t line,
								   std::size_t columns);

	// Where the grid sits on screen and which rows it shows. ScrollOffset counts rows scrolled
	// back from the tail, as the view keeps it.
	struct TerminalGrid
	{
		float OriginX = 0.0f;
		float OriginY = 0.0f;
		float CellWidth = 1.0f;
		float CellHeight = 1.0f;
		std::size_t Columns = 0;
		int VisibleRows = 0;
		int ScrollOffset = 0;
	};

	// The point a position on screen falls on among the rows the grid shows. A position above
	// them lands at the start of the first, one below them at the end of the last, so a sweep
	// out of the grid keeps selecting. Nothing comes back when the grid shows no row.
	std::optional<Core::TextPoint> TerminalPointAt(const Core::TerminalScreen& screen,
												   const TerminalGrid& grid, float x, float y);

	// As TerminalPointAt, but the cell under the pointer (TerminalCellAt) rather than the nearest
	// boundary: Core::SelectionSource::CharacterAt.
	std::optional<Core::TextPoint> TerminalCharacterAt(const Core::TerminalScreen& screen,
													   const TerminalGrid& grid, float x, float y);

	// From the start of the oldest scrollback line to the end of the last screen row that holds
	// anything - text or the cursor - so Select All does not take the blank rows below the prompt.
	std::pair<Core::TextPoint, Core::TextPoint> TerminalEverything(const Core::TerminalScreen& screen,
																   std::size_t columns);

	// The selected text, one line per row, scrollback before screen. Each row's trailing spaces
	// are dropped - the grid is space-filled to its right edge, and nobody typed those - and rows
	// that are no longer there contribute nothing, not even a line break.
	std::string SelectedTerminalText(const Core::TextSelection& selection,
									 const Core::TerminalScreen& screen, std::size_t columns);
}
