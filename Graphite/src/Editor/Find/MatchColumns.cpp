#include "Editor/Find/MatchColumns.h"

#include <algorithm>

namespace Graphite {

	LineMatches MatchColumnsOnLine(const LineMatchInput& input)
	{
		LineMatches result;

		const std::size_t start = input.LineStart;
		const std::size_t lineEnd = start + input.LineText.size();

		// The whole match list is scanned once per *visible line* otherwise, which for a common
		// token in a large file is tens of thousands of iterations per line per frame with the
		// find bar merely open. The list is ascending, so the matches touching this line are one
		// contiguous run: seek to its first member, and stop at the first match beginning past
		// the line's end rather than walking to the document's.
		const auto first = std::lower_bound(
			input.Matches.begin(), input.Matches.end(), start,
			[](const Core::FindMatch& match, std::size_t lineStart) { return match.To <= lineStart; });

		bool currentOnLine = false;

		for (auto it = first; it != input.Matches.end(); ++it)
		{
			const auto [matchFrom, matchTo] = *it;

			if (matchFrom > lineEnd)
			{
				break;
			}

			// Half-open against the line's own span. A match ending exactly at the line's
			// start belongs to the line above. Kept as a test rather than left to the seek
			// above, so what counts as being on the line reads in one place.
			if (matchTo <= start)
			{
				continue;
			}

			const std::size_t fromByte = matchFrom > start ? matchFrom - start : 0;
			const std::size_t toByte = std::min(matchTo - start, input.LineText.size());

			const std::size_t index =
				static_cast<std::size_t>(std::distance(input.Matches.begin(), it));

			if (index == input.CurrentMatch)
			{
				result.Current = result.Columns.size();
				currentOnLine = true;
			}

			result.Columns.push_back(
				MatchColumns{Core::ColumnOfByte(input.LineText, fromByte, input.TabWidth),
							 Core::ColumnOfByte(input.LineText, toByte, input.TabWidth)});
		}

		if (!currentOnLine)
		{
			result.Current = result.Columns.size();
		}

		return result;
	}
}
