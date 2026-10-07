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

	// Far smaller than MaximumFileSize: this text is re-read whenever the working tree changes and
	// lexed again when it differs, so its cost is paid repeatedly rather than once. Two megabytes is
	// past any source file and small enough that re-reading it does not show.
	inline constexpr std::uint64_t MaximumDiffTextSize = 2ull * 1024 * 1024;

	class Repository
	{
	public:
		static Result<Repository> Open(const std::filesystem::path& anyPathInside);

		// A folder that is already a repository is re-initialised in place, which git treats as
		// harmless. The first branch is named from the user's init.defaultBranch.
		static Result<Repository> Init(const std::filesystem::path& existingDirectory);

		// A moved-from Repository holds no handle: every query on it fails, and it is safe to
		// destroy or assign to.
		Repository(Repository&& other) noexcept = default;
		Repository& operator=(Repository&& other) noexcept = default;

		Repository(const Repository&) = delete;
		Repository& operator=(const Repository&) = delete;

		bool IsIgnored(const std::filesystem::path& path) const;

		// A file inside a submodule belongs to the submodule's own repository, so reading it here as
		// untracked would be wrong. The index is asked rather than HEAD because a checkout, a
		// submodule add and a submodule removal all update the index first.
		bool IsInsideSubmodule(const std::filesystem::path& pathRelativeToWorkingDirectory) const;

		// Newest first, in topological then date order. An unborn HEAD answers with an empty,
		// complete page rather than an error.
		Result<LogPage> QueryLog(const LogRequest& request) const;

		// Renames are detected rather than reported as a delete and an add. A binary file comes back
		// flagged and with no hunks.
		Result<Diff> QueryDiff(const DiffRequest& request) const;

		// Ignored files are left out: a build directory would bury everything that can be acted on.
		Result<WorkingTreeStatus> QueryStatus() const;

		// QueryStatus collapses a wholly untracked directory to one entry on purpose, so that a large
		// untracked directory stays cheap to ask about; this opens up one such directory.
		Result<WorkingTreeStatus> QueryUntrackedIn(const std::filesystem::path& directory) const;

		// Local branches first, then remote-tracking ones, each group in name order.
		Result<std::vector<Branch>> QueryBranches() const;

		// No encoding is assumed. NotFound when the commit's tree has no such path, which is the
		// ordinary answer for the older side of an added file.
		Result<std::string> ReadBlob(const std::filesystem::path& path, const Oid& revision) const;

		// NotFound on an unborn HEAD, which is a repository with no commit yet rather than a broken one.
		Result<Oid> HeadCommit() const;

		// Unlike HeadCommit, answers for a repository with no commits and for a detached HEAD.
		Result<HeadState> QueryHead() const;

		// NotFound for a path in conflict, which has one staged content per side of the merge rather
		// than a single one, and for a path that was never staged.
		Result<std::string> ReadIndexBlob(const std::filesystem::path& path) const;

		// NotFound for a path that is gone or is not a regular file: the newer side of a deleted file.
		Result<std::string> ReadWorkingTreeFile(const std::filesystem::path& pathRelativeToWorkingDirectory) const;

		// Empty for a root commit, which has nothing before it; that is not an error.
		Result<std::optional<Oid>> FirstParentOf(const Oid& commit) const;

		// Empty for a stash made without its untracked files. The files a stash's diff lists as added
		// from that commit are in no tree of the stash's own, so their text is read from it.
		Result<std::optional<Oid>> UntrackedOfStash(const Oid& stash) const;

		// Paths are relative to the working directory. One call covers a new, an edited and a deleted
		// file alike, so a caller never needs to know which it has; a conflicted path staged this way
		// stops being conflicted, as git records a resolution.
		Result<void> Stage(const std::vector<std::filesystem::path>& paths);

		// On an unborn HEAD the entry is removed from the index instead, as git does.
		Result<void> Unstage(const std::vector<std::filesystem::path>& paths);

		// Irreversible: what it discards was never in git. Untracked paths are not touched; deleting
		// them is DeleteUntracked, because a caller that means one rarely means both.
		Result<void> DiscardWorkTree(const std::vector<std::filesystem::path>& paths);

		// A path that cannot be removed, such as a file held open by another process, is reported
		// rather than skipped.
		Result<void> DeleteUntracked(const std::vector<std::filesystem::path>& paths);

		// A repository with no user.name or user.email fails here rather than inventing an identity
		// for the history.
		Result<Oid> Commit(const CommitRequest& request);

		// Static, so that a name typed into a dialog is judged before anything is asked of a repository.
		static bool IsValidBranchName(const std::string& name);

		Result<void> CreateBranch(const std::string& name, const Oid& from);
		Result<void> RenameBranch(const std::string& from, const std::string& to);

		// The commits no other local or remote branch reaches: what deleting this one would lose.
		Result<std::size_t> CountUnmergedCommits(const std::string& name) const;

		// Deletes whatever its merge state, so asking first is the caller's job. Refuses a branch
		// checked out here or in another worktree.
		Result<void> DeleteBranch(const std::string& name);

		// False when there was nothing to stash, which a caller stashing on the way to a checkout
		// treats as done. Ignored files are never touched, not even inside an untracked directory, and
		// unresolved conflicts in the index are refused with nothing stashed.
		Result<bool> StashSave(const std::string& message, UntrackedFiles untrackedFiles);

		Result<std::vector<StashEntry>> QueryStashes() const;

		// By id rather than index, because an index names a different stash as soon as another is
		// pushed or dropped. An id no longer in the list is NotFound.
		Result<void> StashApply(const Oid& id);
		Result<void> StashPop(const Oid& id);
		Result<void> StashDrop(const Oid& id);

		// A dry run: changes nothing on disk, in the index or in HEAD.
		Result<CheckoutPlan> PlanCheckout(const CheckoutTarget& target) const;

		// Refused with nothing changed when the target would overwrite changes or the index has
		// unresolved conflicts. An I/O failure on the way can leave the tree partly switched, and the
		// outcome then names every path that may have changed.
		CheckoutOutcome Checkout(const CheckoutTarget& target);

		// Resets the index and the working tree to HEAD, dropping whatever was staged. Ignored files
		// are never touched.
		Result<void> DiscardAll(UntrackedFiles untrackedFiles);

		const std::filesystem::path& GetWorkingDirectory() const
		{
			return m_WorkingDirectory;
		}

	private:
		Repository() = default;

		// Defined in Repository.cpp, because no header under Core/Git includes <git2.h>.
		struct Free
		{
			void operator()(git_repository* repository) const;
		};

		std::unique_ptr<git_repository, Free> m_Handle;
		std::filesystem::path m_WorkingDirectory;
	};
}
