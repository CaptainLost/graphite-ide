#include "corepch.h"

#include "Core/Git/CommitFilter.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/Glob.h"
#include "Core/FileSystem/FileSystem.h"

#include <string_view>

namespace Core {

	namespace {

		constexpr char kRootAnchor = '/';

		bool IsLowercaseHexDigit(char character)
		{
			const bool isDecimalDigit = character >= '0' && character <= '9';
			const bool isLowercaseHexLetter = character >= 'a' && character <= 'f';
			return isDecimalDigit || isLowercaseHexLetter;
		}

		bool IsLowercaseHex(std::string_view text)
		{
			for (const char character : text)
			{
				if (!IsLowercaseHexDigit(character))
				{
					return false;
				}
			}

			return true;
		}

		bool MessageOrAuthorContains(const CommitSummary& commit, std::string_view loweredText)
		{
			return LoweredTextContains(commit.Summary, loweredText) || LoweredTextContains(commit.Body, loweredText) ||
				   LoweredTextContains(commit.Author.Name, loweredText) ||
				   LoweredTextContains(commit.Author.Email, loweredText);
		}
	}

	bool CommitMatchesFilter(const CommitFilter& filter, const CommitSummary& commit)
	{
		const std::string loweredText = Lowered(Trimmed(filter.Text));

		if (loweredText.empty())
		{
			return true;
		}

		if (MessageOrAuthorContains(commit, loweredText))
		{
			return true;
		}

		return IsLowercaseHex(loweredText) && commit.Id.ToHex().starts_with(loweredText);
	}

	bool NamesPath(std::string_view pattern, const std::filesystem::path& path)
	{
		if (pattern.empty())
		{
			return false;
		}

		const std::string forwardSlashPath = ToGenericUtf8(path);

		if (pattern.starts_with(kRootAnchor))
		{
			return GlobMatches(pattern.substr(1), forwardSlashPath);
		}

		const bool patternHasSeparator = pattern.find('/') != std::string_view::npos;

		if (patternHasSeparator)
		{
			return GlobMatches(pattern, forwardSlashPath);
		}

		const std::string fileName = ToGenericUtf8(FromUtf8(forwardSlashPath).filename());
		const bool patternHasWildcard = pattern.find('*') != std::string_view::npos ||
										pattern.find('?') != std::string_view::npos;

		if (patternHasWildcard)
		{
			return GlobMatches(pattern, fileName);
		}

		return LoweredTextContains(fileName, Lowered(pattern));
	}

	std::string PatternNamingOnly(const std::filesystem::path& relativePath)
	{
		std::string pattern = ToGenericUtf8(relativePath);

		if (pattern.find('/') == std::string::npos)
		{
			pattern.insert(pattern.begin(), kRootAnchor);
		}

		return pattern;
	}
}
