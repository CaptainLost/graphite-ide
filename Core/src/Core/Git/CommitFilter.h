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
	// the file name, ignoring case, but never in the directories above it. A leading '/'
	// anchors the pattern to the working directory's root: it is dropped, and the rest is a
	// glob against the whole path, so "/README.md" names the root's README.md alone.
	bool NamesPath(std::string_view pattern, const std::filesystem::path& path);

	// The pattern that names this path, relative to the working directory, and no other. A
	// bare file name would name that name in every folder, so a file at the root is anchored.
	std::string PatternNamingOnly(const std::filesystem::path& relativePath);
}
