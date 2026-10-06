#include "corepch.h"

#include "Core/Base/Glob.h"

#include "Core/Base/Ascii.h"

namespace Core {

	namespace {
		constexpr char kStar = '*';
		constexpr char kQuestionMark = '?';
		constexpr char kSlash = '/';
		constexpr std::size_t kNoStar = std::string_view::npos;

		bool MatchesOneCharacter(char patternCharacter, char textCharacter)
		{
			return patternCharacter == kQuestionMark || EqualsIgnoringCase(patternCharacter, textCharacter);
		}
	}

	// Iterative with a backtrack point rather than recursive: "*a*b*c" against a long path
	// recurses deeply enough to matter, and the loop is no harder to read.
	bool GlobMatches(std::string_view pattern, std::string_view text)
	{
		std::size_t patternIndex = 0;
		std::size_t textIndex = 0;
		std::size_t lastStarInSegment = kNoStar;
		std::size_t starMatchEnd = 0;

		while (textIndex < text.size())
		{
			const bool patternRemains = patternIndex < pattern.size();
			const bool textAtSeparator = text[textIndex] == kSlash;
			const bool patternAtStar = patternRemains && pattern[patternIndex] == kStar;

			if (patternAtStar && !textAtSeparator)
			{
				lastStarInSegment = patternIndex;
				++patternIndex;
				starMatchEnd = textIndex;
				continue;
			}

			const bool patternMatchesCharacter = patternRemains && MatchesOneCharacter(pattern[patternIndex], text[textIndex]);

			if (patternMatchesCharacter && !textAtSeparator)
			{
				++patternIndex;
				++textIndex;
				continue;
			}

			const bool patternAtSeparator = patternRemains && pattern[patternIndex] == kSlash;

			if (patternAtSeparator && textAtSeparator)
			{
				++patternIndex;
				++textIndex;
				lastStarInSegment = kNoStar;
				continue;
			}

			if (lastStarInSegment == kNoStar)
			{
				return false;
			}

			const bool starWouldSwallowSeparator = text[starMatchEnd] == kSlash;

			if (starWouldSwallowSeparator)
			{
				return false;
			}

			patternIndex = lastStarInSegment + 1;
			++starMatchEnd;
			textIndex = starMatchEnd;
		}

		while (patternIndex < pattern.size() && pattern[patternIndex] == kStar)
		{
			++patternIndex;
		}

		return patternIndex == pattern.size();
	}
}
