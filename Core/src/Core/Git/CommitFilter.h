#pragma once

#include "Core/Git/Commit.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace Core {

	struct CommitFilter
	{
		std::string Text;
	};

	// Case and surrounding whitespace are ignored. The text matches as a substring of the
	// summary, the body, or the author's name or email, and as a prefix of the hexadecimal
	// id, which is how git resolves an abbreviated hash.
	bool CommitMatchesFilter(const CommitFilter& filter, const CommitSummary& commit);

	// A pattern with a separator is a glob against the whole path, and one with a wildcard
	// a glob against the whole file name. Any other pattern is a fragment found anywhere in
	// the file name, ignoring case, but never in the directories above it.
	bool NamesPath(std::string_view pattern, const std::filesystem::path& path);
}
