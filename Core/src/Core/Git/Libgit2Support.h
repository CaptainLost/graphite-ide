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
#include <vector>

struct git_oid;
struct git_strarray;

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

	// The pathspec libgit2 takes, built from paths: an array of C strings that has to outlive
	// the call it is handed to, so the converted text is held here and pointed at. Neither
	// copyable nor movable, since the array points into this object's own storage.
	class PathSpec
	{
	public:
		explicit PathSpec(const std::vector<std::filesystem::path>& paths);

		PathSpec(const PathSpec&) = delete;
		PathSpec& operator=(const PathSpec&) = delete;
		PathSpec(PathSpec&&) = delete;
		PathSpec& operator=(PathSpec&&) = delete;

		// Valid for as long as this object lives.
		git_strarray Get() const;

	private:
		std::vector<std::string> m_Text;
		std::vector<const char*> m_Pointers;
	};

	// Idempotent and thread-safe. Every entry point that calls libgit2 without first opening
	// a Repository needs this, since Repository::Open is otherwise what initialises it.
	void EnsureLibgit2Initialised();
}
