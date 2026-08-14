#include "Search/Matcher.h"

#include <cctype>

namespace Graphite {

	namespace {

		// ASCII only, deliberately. A code search compares bytes, and a locale-aware fold would
		// promise a correctness across scripts that a byte comparison cannot deliver.
		char Lower(char c)
		{
			return static_cast<char>(
				std::tolower(static_cast<unsigned char>(c)));
		}

		bool SameChar(char left, char right, bool matchCase)
		{
			return matchCase ? left == right : Lower(left) == Lower(right);
		}

		bool IsIdentifierChar(char c)
		{
			return c == '_' || std::isalnum(static_cast<unsigned char>(c)) != 0;
		}

		// One pattern, * matching any run and ? matching one character. Iterative with a
		// backtrack point rather than recursive: "*a*b*c" against a long name recurses deeply
		// enough to matter, and the loop is no harder to read.
		bool NameMatches(std::string_view pattern, std::string_view name)
		{
			std::size_t p = 0;
			std::size_t n = 0;
			std::size_t star = std::string_view::npos;
			std::size_t afterStar = 0;

			while (n < name.size())
			{
				if (p < pattern.size() && pattern[p] == '*')
				{
					star = p;
					++p;
					afterStar = n;
					continue;
				}

				if (p < pattern.size() &&
					(pattern[p] == '?' || SameChar(pattern[p], name[n], false)))
				{
					++p;
					++n;
					continue;
				}

				if (star == std::string_view::npos)
				{
					return false;
				}

				// The star swallows one more character and we try again from just after it.
				p = star + 1;
				++afterStar;
				n = afterStar;
			}

			while (p < pattern.size() && pattern[p] == '*')
			{
				++p;
			}

			return p == pattern.size();
		}
	}

	bool MaskAccepts(std::string_view mask, std::string_view fileName)
	{
		if (mask.empty())
		{
			return true;
		}

		std::size_t at = 0;

		while (at <= mask.size())
		{
			const std::size_t end = mask.find(';', at);
			const std::size_t stop = end == std::string_view::npos ? mask.size() : end;

			const std::string_view pattern = mask.substr(at, stop - at);

			if (!pattern.empty() && NameMatches(pattern, fileName))
			{
				return true;
			}

			if (end == std::string_view::npos)
			{
				break;
			}

			at = end + 1;
		}

		return false;
	}

	void FindInText(const SearchQuery& query, std::string_view text,
					const std::filesystem::path& file, std::vector<SearchHit>& hits)
	{
		if (query.Needle.empty())
		{
			return;
		}

		const std::string_view needle = query.Needle;

		std::size_t lineStart = 0;
		std::size_t lineNumber = 0;

		while (lineStart <= text.size())
		{
			const std::size_t newline = text.find('\n', lineStart);
			const std::size_t lineEnd = newline == std::string_view::npos ? text.size() : newline;

			std::string_view line = text.substr(lineStart, lineEnd - lineStart);

			if (!line.empty() && line.back() == '\r')
			{
				line.remove_suffix(1);
			}

			for (std::size_t at = 0; at + needle.size() <= line.size();)
			{
				bool same = true;

				for (std::size_t index = 0; index < needle.size(); ++index)
				{
					if (!SameChar(line[at + index], needle[index], query.MatchCase))
					{
						same = false;
						break;
					}
				}

				if (!same)
				{
					++at;
					continue;
				}

				const std::size_t to = at + needle.size();

				const bool bounded =
					!query.WholeWord ||
					((at == 0 || !IsIdentifierChar(line[at - 1])) &&
					 (to == line.size() || !IsIdentifierChar(line[to])));

				if (bounded)
				{
					hits.push_back(SearchHit{file, lineNumber, std::string(line), at, to});

					// Past the match, not one character on: overlapping matches are one match to
					// anyone reading the line.
					at = to;
					continue;
				}

				++at;
			}

			if (newline == std::string_view::npos)
			{
				break;
			}

			lineStart = newline + 1;
			++lineNumber;
		}
	}
}
