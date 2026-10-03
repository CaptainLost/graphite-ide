#pragma once

#include "Core/Base/Result.h"
#include "Core/Git/Checkout.h"
#include "Core/Git/Commit.h"
#include "Core/Git/Diff.h"
#include "Core/Git/Head.h"
#include "Core/Git/Ref.h"
#include "Core/Git/Requests.h"
#include "Core/Git/Stash.h"
#include "Core/Git/Status.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct git_repository;

namespace Core {

	// The ceiling on a file read for a diff's whole-file view. Far smaller than
	// MaximumFileSize, which bounds a document the user asked to open: this text is re-read
	// whenever the working tree changes and lexed again when it differs, so its cost is paid
	// repeatedly rather than once. Two megabytes is past any file that is read as source and
	// small enough that re-reading it does not show.
	inline constexpr std::uint64_t MaximumDiffTextSize = 2ull * 1024 * 1024;

	class Repository
	{
	public:
		static Result<Repository> Open(const std::filesystem::path& anyPathInside);

		// Creates a repository in an existing folder, as `git init` does there. The first
		// branch is named from the user's init.defaultBranch. A folder that is already a
		// repository is re-initialised in place, which git treats as harmless.
		static Result<Repository> Init(const std::filesystem::path& directory);

		// A moved-from Repository holds no handle: every query on it fails, as one on a repository
		// that could not be read does, and it is safe to destroy or assign to.
		Repository(Repository&& other) noexcept = default;
		Repository& operator=(Repository&& other) noexcept = default;

		Repository(const Repository&) = delete;
		Repository& operator=(const Repository&) = delete;

		bool IsIgnored(const std::filesystem::path& path) const;

		// Whether the path, relative to the working directory, lies inside a submodule: some
		// directory above it is a gitlink in the index. Such a file belongs to the
		// submodule's own repository, so this one has nothing to say about it - neither side
		// of it is here, and reading it as untracked would be wrong.
		//
		// The index rather than HEAD, because the index is what a checkout, a submodule add
		// and a submodule removal all update first.
		bool IsInsideSubmodule(const std::filesystem::path& relative) const;

		// Newest first, in topological then date order — the order a history is read in.
		// A repository whose HEAD is unborn answers with an empty, complete page, not an
		// error.
		Result<LogPage> QueryLog(const LogRequest& request) const;

		// Renames are detected rather than reported as a delete and an add. A binary file
		// comes back flagged and with no hunks.
		Result<Diff> QueryDiff(const DiffRequest& request) const;

		// Ignored files are left out: the panel lists what can be acted on, and a build
		// directory would bury everything else.
		Result<WorkingTreeStatus> QueryStatus() const;

		// Every untracked file under one directory, each as its own entry.
		//
		// Separate from QueryStatus rather than folded into it: the full status collapses a
		// wholly untracked directory to a single entry on purpose, so a working tree with a
		// large untracked directory in it stays cheap to ask about. This is what a caller
		// asks once it wants that one directory opened up, and it pays for only that one.
		Result<WorkingTreeStatus> QueryUntrackedIn(const std::filesystem::path& directory) const;

		// Local branches first, then remote-tracking ones, each group in name order.
		Result<std::vector<Branch>> QueryBranches() const;

		// The file's bytes as they stood in one commit. Bytes rather than lines, and no
		// encoding is assumed: the caller knows whether it asked about text.
		//
		// NotFound when that commit's tree has no such path — which is the ordinary answer
		// for the older side of an added file, not a failure worth reporting to anyone.
		Result<std::string> ReadBlob(const std::filesystem::path& path, const Oid& revision) const;

		// The commit HEAD points at. NotFound on an unborn HEAD, which is a repository with
		// no commit yet rather than a broken one - the same answer, and the same meaning, as
		// asking a commit for a parent it does not have.
		Result<Oid> HeadCommit() const;

		// Where HEAD points and what git is in the middle of. Unlike HeadCommit, answers for
		// a repository with no commits and for a detached HEAD rather than failing on them.
		Result<HeadState> QueryHead() const;

		// The file's bytes as they are staged. The two sides of a working-tree diff cannot
		// both come from ReadBlob: a commit is the one thing neither of them is.
		//
		// Only a settled entry answers. A path in conflict has no single staged content —
		// it has one per side of the merge — so this reports NotFound for it rather than
		// picking one and calling it "the" staged version.
		//
		// NotFound is also the ordinary answer for a path that was never staged, which is
		// what the older side of a newly added file looks like.
		Result<std::string> ReadIndexBlob(const std::filesystem::path& path) const;

		// The file's bytes as they are on disk right now, unsaved edits and all. Relative to
		// the working directory, like every other path here.
		//
		// NotFound for a path that is gone or is not a regular file — a deleted file's newer
		// side, which is an answer rather than a failure.
		Result<std::string> ReadWorkingTreeFile(const std::filesystem::path& path) const;

		// The commit this one's diff is taken against. Empty for a root commit, which has
		// nothing before it — not an error, and the caller's answer is the same as for a
		// file that did not exist yet.
		Result<std::optional<Oid>> FirstParentOf(const Oid& commit) const;

		// The commit a stash keeps its untracked files in: its third parent, which only a
		// stash made to include them has. Empty for any other stash, and for a commit with
		// fewer than three parents. The files a stash's diff lists as added from there are
		// in no tree of the stash's own, so their text is read from this one.
		Result<std::optional<Oid>> UntrackedOfStash(const Oid& stash) const;

		// Adds each path to the index as it stands on disk, which is what covers all three
		// cases git otherwise names separately: a new file, an edited one, and a deleted
		// one. Splitting them would push "this file is deleted, so a different call
		// applies" into every caller, which is knowledge the caller does not have and
		// should not acquire.
		//
		// A conflicted path staged this way stops being conflicted — that is how git
		// records a resolution.
		//
		// Paths are relative to the working directory, as FileStatus::Path is.
		Result<void> Stage(const std::vector<std::filesystem::path>& paths);

		// Puts each path back as HEAD has it, leaving the working tree alone. On an unborn
		// HEAD there is nothing to go back to, so the entry is removed from the index
		// instead — the same answer git gives, and not an error.
		Result<void> Unstage(const std::vector<std::filesystem::path>& paths);

		// Overwrites the working tree from the index. Irreversible: what it discards was
		// never in git, so there is nothing to recover it from.
		//
		// Untracked paths are not touched — a path git has never seen has nothing in the
		// index to be restored from. Deleting those is DeleteUntracked, deliberately a
		// separate call: one restores a state git knows, the other destroys a file git
		// does not, and a caller that means one rarely means both.
		Result<void> DiscardWorkTree(const std::vector<std::filesystem::path>& paths);

		// Deletes untracked files outright. A path that cannot be removed — a non-empty
		// directory, a file held open by another process — is reported rather than
		// skipped.
		Result<void> DeleteUntracked(const std::vector<std::filesystem::path>& paths);

		// Writes the index as a commit on HEAD and answers with its id, so a caller has
		// something to show and a test something to assert beyond the absence of an error.
		//
		// Author and committer come from git's own config resolution; a repository with no
		// user.name or user.email fails here rather than inventing an identity for the
		// history.
		Result<Oid> Commit(const CommitRequest& request);

		// The rules git applies to a name under refs/heads/. Static: a name typed into a
		// dialog is judged before anything is asked of a repository.
		static bool IsValidBranchName(const std::string& name);

		Result<void> CreateBranch(const std::string& name, const Oid& from);
		Result<void> RenameBranch(const std::string& from, const std::string& to);

		// Commits only this branch reaches - none of the other local or remote branches
		// does. What deleting it would lose, so the UI can say so before it asks.
		Result<std::size_t> CountUnmergedCommits(const std::string& name) const;

		// Deletes whatever its merge state: asking first is the caller's job. Refuses a
		// branch checked out here or in another worktree.
		Result<void> DeleteBranch(const std::string& name);

		// False when there was nothing to stash - git's own "No local changes to save" -
		// which a caller stashing on the way to a checkout treats as done, not failed.
		// Ignored files are never touched, not even inside an untracked directory, and
		// unresolved conflicts in the index are refused with nothing stashed.
		Result<bool> StashSave(const std::string& message, bool includeUntracked);

		Result<std::vector<StashEntry>> QueryStashes() const;

		// By id rather than index: an index names a different stash as soon as another is
		// pushed or dropped. An id no longer in the list is NotFound.
		Result<void> StashApply(const Oid& id);
		Result<void> StashPop(const Oid& id);
		Result<void> StashDrop(const Oid& id);

		// A dry run: changes nothing on disk, in the index or in HEAD.
		Result<CheckoutPlan> PlanCheckout(const CheckoutTarget& target) const;

		// A safe checkout. When the target would overwrite changes, or the index has
		// unresolved conflicts, it is refused and nothing changes. Otherwise the tree moves to the target and HEAD follows; an I/O failure on
		// the way - a file held open by another process - can leave the tree partly switched,
		// and the outcome then names every path that may have changed.
		CheckoutOutcome Checkout(const CheckoutTarget& target);

		// Resets the index and the working tree to HEAD; removes untracked files only when
		// asked. Ignored files are never touched.
		Result<void> DiscardAll(bool includeUntracked);

		const std::filesystem::path& GetWorkingDirectory() const
		{
			return m_WorkingDirectory;
		}

	private:
		Repository() = default;

		// Frees the repository through git_repository_free, which only Repository.cpp can see:
		// no header under Core/Git includes <git2.h>.
		struct Free
		{
			void operator()(git_repository* repository) const;
		};

		std::unique_ptr<git_repository, Free> m_Handle;
		std::filesystem::path m_WorkingDirectory;
	};
}