#include "corepch.h"

#include "Core/Base/Glob.h"

#include "Core/Base/Ascii.h"

namespace Core {

	namespace {

		bool SameChar(char left, char right)
		{
			return LowerAscii(left) == LowerAscii(right);
		}
	}

	// Iterative with a backtrack point rather than recursive: "*a*b*c" against a long path
	// recurses deeply enough to matter, and the loop is no harder to read.
	bool GlobMatches(std::string_view pattern, std::string_view text)
	{
		std::size_t p = 0;
		std::size_t t = 0;
		std::size_t star = std::string_view::npos;
		std::size_t afterStar = 0;

		while (t < text.size())
		{
			if (p < pattern.size() && pattern[p] == '*' && text[t] != '/')
			{
				star = p;
				++p;
				afterStar = t;
				continue;
			}

			if (p < pattern.size() && text[t] != '/' &&
				(pattern[p] == '?' || SameChar(pattern[p], text[t])))
			{
				++p;
				++t;
				continue;
			}

			// A separator matches only a separator written in the pattern, never a wildcard.
			if (p < pattern.size() && text[t] == '/' && pattern[p] == '/')
			{
				++p;
				++t;

				// The star that was open belonged to the segment just ended; it cannot
				// swallow across the boundary, so there is nothing left to back track to.
				star = std::string_view::npos;
				continue;
			}

			if (star == std::string_view::npos)
			{
				return false;
			}

			// The star would have to swallow the separator to get any further, which is the
			// one thing it may not do.
			if (text[afterStar] == '/')
			{
				return false;
			}

			// The star swallows one more character and we try again from just after it.
			p = star + 1;
			++afterStar;
			t = afterStar;
		}

		while (p < pattern.size() && pattern[p] == '*')
		{
			++p;
		}

		return p == pattern.size();
	}
}
