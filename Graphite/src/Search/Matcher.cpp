#include "Search/Matcher.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/Glob.h"

#include <cctype>

namespace Graphite {

	namespace {

		constexpr char kMaskSeparator = ';';
		constexpr char kNewline = '\n';
		constexpr char kCarriageReturn = '\r';

		bool CharactersMatch(char left, char right, bool matchCase)
		{
			return matchCase ? left == right : Core::EqualsIgnoringCase(left, right);
		}

		bool IsIdentifierCharacter(char character)
		{
			return character == '_' || std::isalnum(static_cast<unsigned char>(character)) != 0;
		}
	}

	bool MaskAccepts(std::string_view mask, std::string_view fileName)
	{
		if (mask.empty())
		{
			return true;
		}

		std::size_t patternStart = 0;

		while (patternStart <= mask.size())
		{
			const std::size_t separatorPosition = mask.find(kMaskSeparator, patternStart);
			const bool isLastPattern = separatorPosition == std::string_view::npos;
			const std::size_t patternEnd = isLastPattern ? mask.size() : separatorPosition;

			const std::string_view pattern = mask.substr(patternStart, patternEnd - patternStart);

			if (!pattern.empty() && Core::GlobMatches(pattern, fileName))
			{
				return true;
			}

			if (isLastPattern)
			{
				break;
			}

			patternStart = separatorPosition + 1;
		}

		return false;
	}

	void FindInLine(const SearchQuery& query, std::string_view line,
					std::vector<LineMatch>& matches)
	{
		if (query.Needle.empty())
		{
			return;
		}

		const std::string_view needle = query.Needle;

		for (std::size_t matchStart = 0; matchStart + needle.size() <= line.size();)
		{
			bool needleMatchesHere = true;

			for (std::size_t needleIndex = 0; needleIndex < needle.size(); ++needleIndex)
			{
				if (!CharactersMatch(line[matchStart + needleIndex], needle[needleIndex],
									 query.MatchCase))
				{
					needleMatchesHere = false;
					break;
				}
			}

			if (!needleMatchesHere)
			{
				++matchStart;
				continue;
			}

			const std::size_t matchEnd = matchStart + needle.size();

			const bool startsOnBoundary =
				matchStart == 0 || !IsIdentifierCharacter(line[matchStart - 1]);
			const bool endsOnBoundary =
				matchEnd == line.size() || !IsIdentifierCharacter(line[matchEnd]);
			const bool satisfiesWholeWord =
				!query.WholeWord || (startsOnBoundary && endsOnBoundary);

			if (satisfiesWholeWord)
			{
				matches.push_back(LineMatch{matchStart, matchEnd});

				// WARNING: Continues past the match, not one character on. Overlapping
				// matches count as one match to anyone reading the line.
				matchStart = matchEnd;
				continue;
			}

			++matchStart;
		}
	}

	void FindInText(const SearchQuery& query, std::string_view text,
					const std::filesystem::path& file, std::vector<SearchHit>& hits)
	{
		if (query.Needle.empty())
		{
			return;
		}

		std::size_t lineStart = 0;
		std::size_t lineNumber = 0;
		std::vector<LineMatch> lineMatches;

		while (lineStart <= text.size())
		{
			const std::size_t newlinePosition = text.find(kNewline, lineStart);
			const bool isLastLine = newlinePosition == std::string_view::npos;
			const std::size_t lineEnd = isLastLine ? text.size() : newlinePosition;

			std::string_view line = text.substr(lineStart, lineEnd - lineStart);

			if (!line.empty() && line.back() == kCarriageReturn)
			{
				line.remove_suffix(1);
			}

			lineMatches.clear();
			FindInLine(query, line, lineMatches);

			for (const LineMatch& match : lineMatches)
			{
				hits.push_back(
					SearchHit{file, lineNumber, std::string(line), match.From, match.To});
			}

			if (isLastLine)
			{
				break;
			}

			lineStart = newlinePosition + 1;
			++lineNumber;
		}
	}
}
