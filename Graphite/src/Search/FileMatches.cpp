#include "Search/FileMatches.h"

#include "Core/Text/TextBuffer.h"
#include "Search/Matcher.h"

namespace Graphite {

	std::vector<DocumentMatch> FindInBuffer(const Core::TextBuffer& buffer,
											const SearchQuery& query)
	{
		std::vector<DocumentMatch> found;

		if (query.Needle.empty())
		{
			return found;
		}

		std::vector<LineMatch> inLine;

		for (std::size_t line = 0; line < buffer.GetLineCount(); ++line)
		{
			const std::string text = buffer.LineText(line);
			const std::size_t start = buffer.LineStart(line);

			inLine.clear();
			FindInLine(query, text, inLine);

			for (const LineMatch& match : inLine)
			{
				found.push_back(DocumentMatch{start + match.From, start + match.To});
			}
		}

		return found;
	}

	std::optional<std::size_t> NextMatch(const std::vector<DocumentMatch>& matches,
										 std::size_t current)
	{
		if (matches.empty())
		{
			return std::nullopt;
		}

		return current + 1 < matches.size() ? current + 1 : 0;
	}

	std::optional<std::size_t> PreviousMatch(const std::vector<DocumentMatch>& matches,
											 std::size_t current)
	{
		if (matches.empty())
		{
			return std::nullopt;
		}

		return current == 0 ? matches.size() - 1 : current - 1;
	}

	std::size_t MatchAtOrAfter(const std::vector<DocumentMatch>& matches, std::size_t offset)
	{
		for (std::size_t index = 0; index < matches.size(); ++index)
		{
			if (matches[index].From >= offset)
			{
				return index;
			}
		}

		return 0;
	}
}
