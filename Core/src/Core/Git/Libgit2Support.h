#pragma once

// No header under Core/Git includes <git2.h>, so the libgit2 types here are only
// forward-declared. ToGitOid and PathSpec::Get return one by value, which a declaration may
// do with an incomplete type, so their callers include <git2.h> themselves.

#include "Core/Base/Result.h"
#include "Core/Git/Commit.h"
#include "Core/Git/GitHandles.h"
#include "Core/Git/Oid.h"
#include "Core/Git/Signature.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

struct git_oid;
struct git_repository;
struct git_strarray;

namespace Core {

	inline constexpr int kHeadIsUnborn = 1;

	inline constexpr unsigned int kFirstParentIndex = 0;

	// A stash's parents are the commit it was made on, its index, and, only for a stash that took
	// untracked files, a commit holding those files alone, so that commit's whole tree was added.
	inline constexpr unsigned int kUntrackedFilesParentOfStash = 2;

	// libgit2 keeps the last error per thread, so call this on the thread whose call failed.
	std::string LastLibgit2Message();

	Oid ToOid(const git_oid& id);

	git_oid ToGitOid(const Oid& id);

	std::string TextOrEmpty(const char* text);

	// An empty view may carry a null pointer, and libgit2 reads a null buffer as no file at
	// all, which is not what an empty text means.
	const char* DataOrEmptyString(std::string_view text);

	// What a Repository call returns when Open never succeeded.
	Error RepositoryNotOpen();

	// NotFound, naming the id, when the repository holds no such commit.
	Result<CommitHandle> LookupCommit(git_repository* handle, const Oid& id);

	Signature ToSignature(const git_signature* signature);

	// Summary is the first line of the message and Body what follows the one blank line after it,
	// with the message's own line ends kept.
	CommitSummary ToCommitSummary(git_commit* commit);

	// git expects forward slashes on every platform.
	std::string ToGitPath(const std::filesystem::path& relative);

	// libgit2's pathspec is an array of C strings pointing into this object's storage, so the
	// object is neither copyable nor movable and must outlive the call it is handed to.
	class PathSpec
	{
	public:
		explicit PathSpec(const std::vector<std::filesystem::path>& paths);

		PathSpec(const PathSpec&) = delete;
		PathSpec& operator=(const PathSpec&) = delete;
		PathSpec(PathSpec&&) = delete;
		PathSpec& operator=(PathSpec&&) = delete;

		git_strarray Get() const;

	private:
		std::vector<std::string> m_GitPaths;
		std::vector<const char*> m_GitPathPointers;
	};

	// Untracked files go one by one, never a whole directory: removing a directory whole would take
	// the ignored files under it too. A directory in the list, a nested repository, is left alone, as
	// `git clean -d` leaves one, and a directory a removal leaves empty goes with it.
	Result<void> RemoveUntrackedFiles(const std::filesystem::path& workingDirectory,
									  const std::vector<std::filesystem::path>& relativeFiles);

	// libgit2 caches one index per repository handle and does not always reload it, so every
	// operation that reads or writes the index, or makes a libgit2 call that does, opens it
	// through this first. Never between an operation's own edits and its write: the reload
	// discards unsaved changes.
	Result<IndexHandle> OpenFreshIndex(git_repository* handle);

	// For an operation that does not use the index itself but needs its next libgit2 call to
	// see the current one.
	Result<void> RefreshIndex(git_repository* handle);

	// Call before any libgit2 call that no Repository::Open precedes, since that is otherwise
	// what initialises the library. Safe to call repeatedly from any thread.
	void EnsureLibgit2Initialised();
}
