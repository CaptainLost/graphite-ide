#pragma once

// Small conversions shared by every Repository query. Kept here instead of copied into
// each RepositoryXxx.cpp so the five near-identical bodies that used to exist under
// different names cannot drift from one another again.
//
// The rule that no header under Core/Git may include <git2.h> still applies: the libgit2
// types stay forward-declared here, with the definitions living in the one .cpp that does
// include git2.h. ToGitOid returns git_oid by value below - legal for an incomplete type
// in a declaration - so "touched by reference or pointer" describes the other functions
// here, not that one.

#include "Core/Git/Oid.h"

#include <filesystem>
#include <string>

struct git_oid;

namespace Core {

	// libgit2's own message names the real cause of the last failed call on this thread -
	// a .git file pointing at a deleted worktree reads very differently from an unreadable
	// directory.
	std::string LastLibgit2Message();

	// A git object name read out of libgit2's own representation.
	Oid ToOid(const git_oid& id);

	// The inverse: this codebase's Oid back into the twenty raw bytes libgit2 expects.
	git_oid ToGitOid(const Oid& id);

	// git speaks forward slashes on every platform; a relative path may carry native
	// separators.
	std::string ToGitPath(const std::filesystem::path& relative);
}
