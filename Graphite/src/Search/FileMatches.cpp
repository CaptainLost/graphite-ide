#include "Search/FileMatches.h"

#include "Core/Text/TextBuffer.h"
#include "Search/Matcher.h"

namespace Graphite {

	namespace {

		constexpr std::size_t kFirstMatchIndex = 0;
	}

	// WARNING: Walks the buffer a line at a time instead of materialising it. This runs on
	// every keystroke in the query field, and a 16 MB file would otherwise be copied each time.
	std::vector<DocumentMatch> FindInBuffer(const Core::TextBuffer& buffer,
											const SearchQuery& query)
	{
		std::vector<DocumentMatch> documentMatches;

		if (query.Needle.empty())
		{
			return documentMatches;
		}

		std::vector<LineMatch> lineMatches;

		for (std::size_t line = 0; line < buffer.GetLineCount(); ++line)
		{
			const std::string lineText = buffer.LineText(line);
			const std::size_t lineStart = buffer.LineStart(line);

			lineMatches.clear();
			FindInLine(query, lineText, lineMatches);

			for (const LineMatch& match : lineMatches)
			{
				documentMatches.push_back(
					DocumentMatch{lineStart + match.From, lineStart + match.To});
			}
		}

		return documentMatches;
	}

	std::optional<std::size_t> NextMatch(const std::vector<DocumentMatch>& matches,
										 std::size_t current)
	{
		if (matches.empty())
		{
			return std::nullopt;
		}

		const std::size_t following = current + 1;

		return following < matches.size() ? following : kFirstMatchIndex;
	}

	std::optional<std::size_t> PreviousMatch(const std::vector<DocumentMatch>& matches,
											 std::size_t current)
	{
		if (matches.empty())
		{
			return std::nullopt;
		}

		const std::size_t lastMatchIndex = matches.size() - 1;

		return current == kFirstMatchIndex ? lastMatchIndex : current - 1;
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

		return kFirstMatchIndex;
	}
}
