#include "Terminal/TerminalSearch.h"

#include "Terminal/TerminalSelection.h"

#include <algorithm>
#include <string>

namespace Graphite {

	namespace {

		char LoweredAscii(char character)
		{
			return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
		}

		std::string Lowered(std::string_view text)
		{
			std::string lowered(text);
			std::ranges::transform(lowered, lowered.begin(), LoweredAscii);
			return lowered;
		}

		// The first cell that starts at `byte` or later and is not the second half of a wide character; `visibleCount`
		// when none does.
		std::size_t CellStartingAt(const std::vector<Core::TerminalCell>& cells, const std::vector<std::size_t>& cellBytes,
								   std::size_t visibleCount, std::size_t from, std::size_t byte)
		{
			for (std::size_t column = from; column < visibleCount; ++column)
			{
				if (cellBytes[column] >= byte && !cells[column].WideTrailer)
				{
					return column;
				}
			}

			return visibleCount;
		}
	}

	std::vector<TerminalMatch> FindAllInTerminal(const Core::TerminalScreen& screen, std::size_t columns,
												 std::string_view needle)
	{
		std::vector<TerminalMatch> matches;

		if (needle.empty() || columns == 0)
		{
			return matches;
		}

		const std::string loweredNeedle = Lowered(needle);
		const std::uint64_t rowsScrolledOff = screen.GetRowsScrolledOff();
		const std::int64_t firstLine = LineOfScrollbackRow(rowsScrolledOff, screen.GetScrollbackCount(), 0);
		const std::int64_t lastLine = LineOfScreenRow(rowsScrolledOff, static_cast<int>(screen.GetRows()) - 1);

		std::vector<Core::TerminalCell> rowCopy;

		for (std::int64_t line = firstLine; line <= lastLine; ++line)
		{
			const std::vector<Core::TerminalCell>* cells = TerminalLineCells(screen, line, columns, rowCopy);

			if (cells == nullptr)
			{
				continue;
			}

			const std::size_t visibleCount = TerminalLineLength(screen, line, columns);
			const std::string text = Lowered(TerminalRowText(*cells, visibleCount));
			const std::vector<std::size_t> cellBytes = TerminalCellBytes(*cells, visibleCount);

			for (std::size_t found = text.find(loweredNeedle); found != std::string::npos;
				 found = text.find(loweredNeedle, found + loweredNeedle.size()))
			{
				const std::size_t from = CellStartingAt(*cells, cellBytes, visibleCount, 0, found);
				const std::size_t to = CellStartingAt(*cells, cellBytes, visibleCount, from + 1, found + loweredNeedle.size());
				matches.push_back(TerminalMatch{.Line = line, .FromColumn = from, .ToColumn = to});
			}
		}

		return matches;
	}

	std::optional<std::size_t> NextTerminalMatch(const std::vector<TerminalMatch>& matches,
												 const std::optional<TerminalMatch>& current,
												 TerminalSearchDirection direction)
	{
		if (matches.empty())
		{
			return std::nullopt;
		}

		if (!current.has_value())
		{
			return matches.size() - 1;
		}

		if (direction == TerminalSearchDirection::Forward)
		{
			const auto after = std::ranges::upper_bound(matches, *current);
			return after == matches.end() ? 0 : static_cast<std::size_t>(after - matches.begin());
		}

		const auto atOrAfter = std::ranges::lower_bound(matches, *current);
		return atOrAfter == matches.begin() ? matches.size() - 1 : static_cast<std::size_t>(atOrAfter - matches.begin()) - 1;
	}
}
