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

	// WARNING: Only rows pushed into the scrollback move a line number, so rows scrolling inside a
	// DECSTBM region, or on a screen whose scrollback limit is 0, keep their numbers while their text
	// moves under them.
	std::int64_t LineOfScrollbackRow(std::uint64_t rowsScrolledOff, std::size_t scrollbackCount,
									 std::size_t index);

	std::int64_t LineOfScreenRow(std::uint64_t rowsScrolledOff, int screenRow);

	bool SelectionLeftHistory(const Core::TextSelection& selection, std::uint64_t rowsScrolledOff,
							  std::size_t scrollbackCount);

	Core::TextSelection ClampSelectionToHistory(const Core::TextSelection& selection,
												std::uint64_t rowsScrolledOff, std::size_t scrollbackCount);

	std::vector<std::size_t> TerminalCellBytes(const std::vector<Core::TerminalCell>& cells, std::size_t columns);

	std::string TerminalRowText(const std::vector<Core::TerminalCell>& cells, std::size_t columns);

	Core::ColumnSpan TerminalWordAt(const std::vector<Core::TerminalCell>& cells, std::size_t columns,
									std::size_t column);

	std::size_t TerminalColumnAt(const std::vector<Core::TerminalCell>& cells, std::size_t columns,
								 float position);

	std::size_t TerminalCellAt(const std::vector<Core::TerminalCell>& cells, std::size_t columns,
							   float position);

	const std::vector<Core::TerminalCell>* TerminalLineCells(const Core::TerminalScreen& screen,
															 std::int64_t line, std::size_t columns,
															 std::vector<Core::TerminalCell>& rowCopy);

	std::size_t TerminalLineLength(const Core::TerminalScreen& screen, std::int64_t line,
								   std::size_t columns);

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

	std::optional<Core::TextPoint> TerminalPointAt(const Core::TerminalScreen& screen,
												   const TerminalGrid& grid, float x, float y);

	std::optional<Core::TextPoint> TerminalCharacterAt(const Core::TerminalScreen& screen,
													   const TerminalGrid& grid, float x, float y);

	std::pair<Core::TextPoint, Core::TextPoint> TerminalEverything(const Core::TerminalScreen& screen,
																   std::size_t columns);

	std::string SelectedTerminalText(const Core::TextSelection& selection,
									 const Core::TerminalScreen& screen, std::size_t columns);
}
