#include "corepch.h"

#include "Core/Git/CommitFilter.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/Glob.h"

#include <string_view>

namespace Core {

	namespace {

		bool Contains(std::string_view haystack, std::string_view lowered)
		{
			return Lowered(haystack).find(lowered) != std::string::npos;
		}

		bool IsHex(std::string_view text)
		{
			for (const char digit : text)
			{
				const bool hex = (digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f');

				if (!hex)
				{
					return false;
				}
			}

			return true;
		}
	}

	bool Matches(const CommitFilter& filter, const CommitSummary& commit)
	{
		const std::string text = Lowered(Trimmed(filter.Text));

		if (text.empty())
		{
			return true;
		}

		if (Contains(commit.Summary, text) || Contains(commit.Body, text) ||
			Contains(commit.Author.Name, text) || Contains(commit.Author.Email, text))
		{
			return true;
		}

		// Checked last: a text that reads as both — "added", "beef" — is far more likely to
		// be a word the reader remembers than the opening digits of a hash, and answering
		// the message first costs nothing, since a text that really is a hash prefix will
		// not be found in any of the fields above.
		return IsHex(text) && commit.Id.ToHex().starts_with(text);
	}

	bool NamesPath(std::string_view pattern, const std::filesystem::path& path)
	{
		if (pattern.empty())
		{
			return false;
		}

		// Git writes its paths with forward slashes whatever the platform, and so must
		// whichever side of this comparison came from a filesystem::path.
		const std::string text = path.generic_string();

		if (pattern.find('/') != std::string_view::npos)
		{
			return GlobMatches(pattern, text);
		}

		return GlobMatches(pattern, std::filesystem::path{text}.filename().generic_string());
	}
}
