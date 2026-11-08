#pragma once

#include "Core/Base/Result.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Blame.h"
#include "Core/Git/Checkout.h"
#include "Core/Git/Commit.h"
#include "Core/Git/Content/ContentAnswer.h"
#include "Core/Git/Diff.h"
#include "Core/Git/Head.h"
#include "Core/Git/Lines/AncestryGraph.h"
#include "Core/Git/LineHistory.h"
#include "Core/Git/OwnAuthors.h"
#include "Core/Git/PatchMatch.h"
#include "Core/Git/Remote.h"
#include "Core/Git/Ref.h"
#include "Core/Git/Requests.h"
#include "Core/Git/Stash.h"
#include "Core/Git/Status.h"
#include "Core/Git/Tag.h"
#include "Core/Git/Worktree.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

struct git_odb_backend;
struct git_repository;

namespace Core {

	// Whether objects a check creates reach the repository. InMemory keeps them in a store that is
	// dropped, so a read-only question leaves the object directory as it found it.
	enum class ObjectWrites
	{
		OnDisk,
		InMemory,
	};

	// Far smaller than MaximumFileSize: this text is re-read whenever the working tree changes and
	// lexed again when it differs, so its cost is paid repeatedly rather than once. Two megabytes is
	// past any source file and small enough that re-reading it does not show.
	inline constexpr std::uint64_t MaximumDiffTextSize = 2ull * 1024 * 1024;

	class Repository
	{
	public:
		static Result<Repository> Open(const std::filesystem::path& anyPathInside);

		static Result<Repository> Open(const std::filesystem::path& anyPathInside, ObjectWrites writes);

		// The repository this directory is the working directory of, or the git directory of, and
		// no other: nothing is searched for above it, so a worktree folder that lost its `.git` file
		// answers NotFound rather than the repository around it.
		static Result<Repository> OpenExactly(const std::filesystem::path& directory);

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

		std::vector<std::filesystem::path> IgnoreRuleFilesOutside(const std::filesystem::path& folder) const;

		// A file inside a submodule belongs to the submodule's own repository, so reading it here as
		// untracked would be wrong. The index is asked rather than HEAD because a checkout, a
		// submodule add and a submodule removal all update the index first.
		bool IsInsideSubmodule(const std::filesystem::path& pathRelativeToWorkingDirectory) const;

		// Newest first, in topological then date order. An unborn HEAD answers with an empty,
		// complete page rather than an error.
		Result<LogPage> QueryLog(const LogRequest& request) const;

		// Each line of the file as it is at request.At, with the commit that last changed it and the
		// path and line it had there; a rename is followed. NotFound when the path is not a file in
		// that commit.
		Result<Blame> QueryBlame(const BlameRequest& request) const;

		// One line followed back along first parents to the commit that added it, listing every
		// commit that changed it on the way, newest first. A rename is followed. Not Complete when
		// MaxWalk or a diff that cannot be read stopped the walk first, or when a root commit was
		// reached without the file in it.
		Result<LineHistory> QueryLineHistory(const LineHistoryRequest& request) const;

		// The commits reachable from `tip`, itself included, as `git rev-list --count` gives them;
		// with FirstOnly, as `git rev-list --first-parent --count` does, the rows of a first-parent
		// history. Reads only the commit graph, not the commits' messages. NotFound for a commit the
		// object database does not hold.
		Result<std::size_t> CountCommits(const Oid& tip, FollowedParents parents = FollowedParents::All) const;

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

		// Every refs/tags/* that peels to a commit this repository holds, newest first by When, then by Name. A tag
		// of a tree or a blob, one whose commit is missing and a symbolic ref are left out; only a failure that is not
		// a missing object fails the listing.
		Result<std::vector<Tag>> QueryTags(TagCache& cache) const;

		// One walk over every tip with the hidden commits left out, then each tip's own commits
		// gathered from what that walk found.
		Result<std::vector<TipAuthors>> QueryOwnAuthors(const OwnAuthorsRequest& request) const;

		// Every commit reachable from Tips and not from Known, with parents, committer time and author key.
		// Commits listed in the repository's shallow file come back with ParentsCut and no parents.
		Result<std::vector<GraphCommit>> ReadGraphCommits(const GraphReadRequest& request) const;

		bool IsShallow() const;

		// The commits the shallow file names, whose parents this repository does not have; none for a whole clone.
		// ReadGraphCommits returns each of them cut.
		std::unordered_set<Oid> ShallowBoundary() const;

		// The git directory every worktree of this repository shares (git_repository_commondir).
		std::filesystem::path CommonDirectory() const;

		// Configured remotes, in name order.
		Result<std::vector<std::string>> QueryRemoteNames() const;

		// Sorted by name.
		Result<std::vector<Remote>> QueryRemotes() const;

		// Empty when no configuration level sets user.email, or sets it blank.
		Result<std::optional<std::string>> ReadUserEmail() const;

		// No encoding is assumed. NotFound when the commit's tree has no such path, which is the
		// ordinary answer for the older side of an added file.
		Result<std::string> ReadBlob(const std::filesystem::path& path, const Oid& revision) const;

		// NotFound when the object database does not hold the blob: a version that was only ever on
		// disk, or one `git gc` pruned after a rewrite.
		Result<std::string> ReadBlobById(const Oid& blob) const;

		// Two texts compared as one file at `path`. Equal texts give a file with no hunks. Needs no
		// repository, so it is static; the caller sets the blob ids it knows.
		static Result<FileDiff> DiffTexts(std::string_view oldText, std::string_view newText,
										  const std::filesystem::path& path, WhitespaceInDiff whitespace);

		// NotFound on an unborn HEAD, which is a repository with no commit yet rather than a broken one.
		Result<Oid> HeadCommit() const;

		// A full name such as "refs/heads/main" or "refs/remotes/origin/main". NotFound when there
		// is no such reference, which is the ordinary answer for a branch deleted since it was listed.
		Result<Oid> ResolveReference(const std::string& fullName) const;

		// The best common ancestor, as `git merge-base` gives it. NotFound when the two histories
		// share no commit, which is an answer rather than a failure.
		Result<Oid> MergeBase(const Oid& one, const Oid& other) const;

		// The working directory the branch is checked out in: this repository's own, a linked
		// worktree's, or - asked from a linked worktree - the main checkout's. Empty when none holds
		// it; a worktree whose HEAD is detached, as during a rebase, holds no branch, and nor does a
		// linked worktree whose folder is gone.
		Result<std::optional<std::filesystem::path>> WorktreeHolding(const std::string& branchFullName) const;

		// The main checkout first, then the linked worktrees in name order. Asked from a linked
		// worktree, the same list. A bare repository has no main checkout, so only its linked ones.
		Result<std::vector<WorktreeEntry>> QueryWorktrees() const;

		// Like `git cherry base tip`: a commit of tip's own is in the base when a commit the base has and
		// tip lacks carries the same patch id. NotFound when either commit is missing. Reads at most
		// `budget` of the base's commits; Complete says whether that was all of them. A patch that cannot
		// be read for a missing object, as in a blobless clone, stops the walk with MissingObjects set: an
		// answer, not a failure.
		Result<PatchMatch> MatchPatches(const Oid& tip, const Oid& base, std::size_t budget, PatchIdCache& cache) const;

		// The newest commit lineTip reaches and `commit` does not that carries `commit`'s patch: its copy
		// on that line, among the first `budget` of those commits read newest first. No copy for a commit
		// with no patch of its own; MoreToRead when the budget ran out before the line commits did, so a
		// larger budget may still find one. The commits read first are the same for any budget, so a
		// search continued with a larger budget answers as one search with that budget would. NotFound
		// when either commit, or an object a patch needs, is missing.
		Result<PatchTwinSearch> FindPatchTwin(const Oid& commit, const Oid& lineTip, std::size_t budget,
											  PatchIdCache& cache) const;

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

		// Yes when merging branchTip into lineTip in memory gives lineTip's own tree, so the line already
		// holds everything the branch has; No on a conflict or another tree. Unknown when an object the
		// merge needs is missing, as in a blobless clone. Writes nothing into the repository. Needs a
		// repository opened with ObjectWrites::InMemory, else InvalidArgument.
		Result<ContentAnswer> MergeChangesNothing(const Oid& lineTip, const Oid& branchTip) const;

		// Forgets what in-memory checks wrote; a check does this itself when it ends.
		void DropObjectsInMemory() const;

		// Whether the repository has a commit with this id: false when the object database lacks it (IsMissingObject), and
		// IoFailure when it cannot be read for another reason.
		Result<bool> HoldsCommit(const Oid& commit) const;

		// Whether the two commits record the same tree. NotFound when either is missing.
		Result<bool> HaveSameTree(const Oid& one, const Oid& other) const;

		// The oldest `mostCommits` commits of lineTip's first-parent chain that `since` does not reach and
		// that change a path branchTip changes since `since`, oldest first: where a squash of the branch can
		// have landed. Reads at most `chainBudget` commits of the chain, and answers none when the chain is
		// longer, since the oldest could lie past them. Reads trees only, never a file's contents. NotFound
		// when an object the walk needs is missing.
		Result<std::vector<Oid>> LineCommitsTouchingBranch(const Oid& lineTip, const Oid& since, const Oid& branchTip,
														   std::size_t chainBudget, std::size_t mostCommits) const;

		// The paths at which the two commits' trees differ, sorted: a renamed file counts at both its paths, since no
		// rename is detected. Reads trees only, never a file's contents. NotFound when a commit or a tree is missing.
		Result<std::vector<std::string>> PathsChangedBetween(const Oid& older, const Oid& newer) const;

		// Whether the two commits' trees differ at any of `paths`, each a file's or a directory's path taken as written (no
		// pattern is expanded), so only the trees those paths lie in are read. False for no paths. Reads trees only.
		// NotFound when a commit or a tree is missing.
		Result<bool> TreesDifferAt(const Oid& from, const Oid& to, const std::vector<std::string>& paths) const;

		const std::filesystem::path& GetWorkingDirectory() const
		{
			return m_WorkingDirectory;
		}

	private:
		Repository() = default;

		// The flags are libgit2's GIT_REPOSITORY_OPEN_* bits.
		static Result<Repository> OpenWith(const std::filesystem::path& where, unsigned int openFlags, ObjectWrites writes);

		Result<void> AttachMemoryBackend();

		// Defined in Repository.cpp, because no header under Core/Git includes <git2.h>.
		struct Free
		{
			void operator()(git_repository* repository) const;
		};

		std::unique_ptr<git_repository, Free> m_Handle;
		// Owned by the object database of m_Handle, so it dangles once a move has emptied m_Handle.
		git_odb_backend* m_MemoryBackend = nullptr;
		std::filesystem::path m_WorkingDirectory;
		PathKeyText m_WorkingDirectoryKey;
		std::vector<std::filesystem::path> m_GlobalExcludesFiles;
	};
}
