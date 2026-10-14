#pragma once

#include "Core/Text/Columns.h"
#include "Core/Text/DocumentSet.h"

#include <cstddef>
#include <string_view>
#include <vector>

namespace Graphite {

	struct MatchColumns
	{
		std::size_t FromColumn = 0;
		std::size_t ToColumn = 0;
	};

	struct LineMatches
	{
		std::vector<MatchColumns> Columns;

		// WARNING: Columns.size() means "the current match is on another line". Zero is a real
		// index, and using it as "none" paints the first match of every line as the current one.
		std::size_t Current = 0;
	};

	struct LineMatchInput
	{
		const std::vector<Core::FindMatch>& Matches;
		std::string_view LineText;
		std::size_t LineStart = 0;
		std::size_t CurrentMatch = 0;
		std::size_t TabWidth = Core::DefaultTabWidth;
	};

	LineMatches MatchColumnsOnLine(const LineMatchInput& input);
}
