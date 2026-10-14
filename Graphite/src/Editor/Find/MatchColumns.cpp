#include "Editor/Find/MatchColumns.h"

#include <algorithm>
#include <iterator>

namespace Graphite {

	namespace {

		using MatchIterator = std::vector<Core::FindMatch>::const_iterator;

		bool EndsAtOrBefore(const Core::FindMatch& match, std::size_t lineStart)
		{
			return match.To <= lineStart;
		}
	}

	LineMatches MatchColumnsOnLine(const LineMatchInput& input)
	{
		LineMatches result;

		const std::size_t lineStart = input.LineStart;
		const std::size_t lineEnd = lineStart + input.LineText.size();

		// WARNING: The matches are ascending, so those touching this line are one contiguous run.
		// Seeking to its first member keeps the cost per visible line off the size of the document.
		const MatchIterator firstOnLine = std::lower_bound(
			input.Matches.begin(), input.Matches.end(), lineStart, EndsAtOrBefore);

		bool currentOnLine = false;

		for (MatchIterator matchIterator = firstOnLine; matchIterator != input.Matches.end();
			 ++matchIterator)
		{
			const auto [matchFrom, matchTo] = *matchIterator;

			if (matchFrom > lineEnd)
			{
				break;
			}

			if (matchTo <= lineStart)
			{
				continue;
			}

			const std::size_t fromByte = matchFrom > lineStart ? matchFrom - lineStart : 0;
			const std::size_t toByte = std::min(matchTo - lineStart, input.LineText.size());

			const std::size_t matchIndex =
				static_cast<std::size_t>(std::distance(input.Matches.begin(), matchIterator));

			if (matchIndex == input.CurrentMatch)
			{
				result.Current = result.Columns.size();
				currentOnLine = true;
			}

			const std::size_t fromColumn = Core::ColumnOfByte(input.LineText, fromByte, input.TabWidth);
			const std::size_t toColumn = Core::ColumnOfByte(input.LineText, toByte, input.TabWidth);

			result.Columns.push_back(MatchColumns{.FromColumn = fromColumn, .ToColumn = toColumn});
		}

		if (!currentOnLine)
		{
			result.Current = result.Columns.size();
		}

		return result;
	}
}
