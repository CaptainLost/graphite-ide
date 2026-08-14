#pragma once

#include "Core/Text/Columns.h"

#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

namespace Graphite {

	// A match's span across one line, in columns of the *expanded* text — tabs already widened
	// into the cells they occupy. Distinct from Search/Matcher.h's LineMatch, which holds the
	// same shape in raw bytes: the editor draws expanded text, so a byte offset used as a column
	// lands several cells out on any line holding a tab.
	struct MatchColumns
	{
		std::size_t FromColumn = 0;
		std::size_t ToColumn = 0;
	};

	struct LineMatches
	{
		std::vector<MatchColumns> Columns;

		// The index within Columns of the current match, or Columns.size() when the current
		// match is on another line. Not zero: zero is a real index, and using it as "none" paints
		// the first match of every line as the current one.
		std::size_t Current = 0;
	};

	// Bundled rather than passed one at a time: three of these are std::size_t, and in a
	// parameter list a caller can transpose two of them with nothing said. Fill the size_t fields
	// by name — positional aggregate initialisation would give the transposition back.
	struct LineMatchInput
	{
		// Document byte ranges, ascending and non-overlapping. The ascending part is load-bearing:
		// the line's own matches are a contiguous run, which is what lets the search below skip
		// everything above the line instead of walking the whole document per line.
		const std::vector<std::pair<std::size_t, std::size_t>>& Matches;

		// The line as it is stored, tabs included — the conversion needs them.
		std::string_view LineText;

		// The line's first byte in the document.
		std::size_t LineStart = 0;

		// Index into Matches of the match the caret is on.
		std::size_t CurrentMatch = 0;

		std::size_t TabWidth = Core::DefaultTabWidth;
	};

	LineMatches MatchColumnsOnLine(const LineMatchInput& input);
}
