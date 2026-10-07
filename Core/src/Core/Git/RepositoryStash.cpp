#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/Git/GitHandles.h"
#include "Core/Git/Libgit2Support.h"

#include <git2.h>

#include <cstdint>
#include <format>
#include <string>
#include <vector>

namespace Core {

	namespace {

		constexpr int kStopListing = -1;
		constexpr int kContinueListing = 0;

		struct StashListing
		{
			git_repository* RepositoryHandle = nullptr;
			std::vector<StashEntry> Entries;
			bool HitUnreadableCommit = false;
		};

		// A stash whose commit cannot be read is reported, not listed with a zero time: the list is
		// what the writes resolve an id against, and a half-read entry would make a present stash look
		// absent or a stale one look current.
		int AppendStashEntry(std::size_t stashIndex, const char* message, const git_oid* stashId, void* payload)
		{
			StashListing& listing = *static_cast<StashListing*>(payload);

			CommitHandle stashCommit;

			if (git_commit_lookup(Out(stashCommit), listing.RepositoryHandle, stashId) != 0)
			{
				listing.HitUnreadableCommit = true;
				return kStopListing;
			}

			StashEntry entry;
			entry.Index = stashIndex;
			entry.Message = TextOrEmpty(message);
			entry.Id = ToOid(*stashId);
			entry.SecondsSinceEpoch = static_cast<std::int64_t>(git_commit_time(stashCommit.get()));

			listing.Entries.push_back(std::move(entry));
			return kContinueListing;
		}

		Result<std::vector<StashEntry>> ListStashes(git_repository* handle)
		{
			StashListing listing{.RepositoryHandle = handle, .Entries = {}, .HitUnreadableCommit = false};
			const int foreachResult = git_stash_foreach(handle, AppendStashEntry, &listing);

			if (listing.HitUnreadableCommit)
			{
				return Error{ErrorCode::IoFailure, std::format("cannot read a stash commit: {}", LastLibgit2Message())};
			}

			if (foreachResult != 0)
			{
				return Error{ErrorCode::IoFailure, std::format("cannot list stashes: {}", LastLibgit2Message())};
			}

			return std::move(listing.Entries);
		}

		// An error from listing is passed on as itself rather than turned into "not found": a stash
		// that could not be looked for is not gone.
		Result<std::size_t> FindStashPosition(git_repository* handle, const Oid& stashId)
		{
			Result<std::vector<StashEntry>> stashes = ListStashes(handle);

			if (!stashes.HasValue())
			{
				return stashes.GetError();
			}

			for (const StashEntry& entry : stashes.GetValue())
			{
				if (entry.Id == stashId)
				{
					return entry.Index;
				}
			}

			return Error{ErrorCode::NotFound, "that stash no longer exists"};
		}

		// libgit2 reports a stash applied with conflicts against the branch as a success and leaves
		// the conflicts in the index.
		Result<bool> IndexHasConflicts(git_repository* handle)
		{
			Result<IndexHandle> freshIndex = OpenFreshIndex(handle);

			if (!freshIndex.HasValue())
			{
				return freshIndex.GetError();
			}

			return git_index_has_conflicts(freshIndex.GetValue().get()) != 0;
		}
	}

	Result<bool> Repository::StashSave(const std::string& message, UntrackedFiles untrackedFiles)
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		SignatureHandle stasher;

		if (git_signature_default(Out(stasher), m_Handle.get()) != 0)
		{
			return Error{ErrorCode::InvalidArgument,
						 std::format("cannot stash without a git identity: {}", LastLibgit2Message())};
		}

		// The stash commits the handle's cached index as the stash's index commit. libgit2's own
		// check for changes reloads it first today, but that is not a promise it makes.
		if (const Result<void> refreshed = RefreshIndex(m_Handle.get()); !refreshed.HasValue())
		{
			return refreshed.GetError();
		}

		// Left to itself libgit2 clears the tree after stashing, and with untracked files it clears
		// through REMOVE_UNTRACKED, which deletes an untracked directory whole, ignored files inside it
		// included. So the stash only records, and DiscardAll clears the tree file by file.
		std::uint32_t stashFlags = GIT_STASH_KEEP_ALL;

		if (untrackedFiles == UntrackedFiles::Included)
		{
			stashFlags |= GIT_STASH_INCLUDE_UNTRACKED;
		}

		const char* messageOrDefault = message.empty() ? nullptr : message.c_str();

		git_oid stashCommitId{};
		const int saveResult = git_stash_save(&stashCommitId, m_Handle.get(), stasher.get(), messageOrDefault, stashFlags);

		if (saveResult == GIT_ENOTFOUND)
		{
			return false;
		}

		// libgit2 cannot write a tree from a conflicted index, and says so in its own terms; it
		// refuses before recording anything.
		if (saveResult == GIT_EUNMERGED)
		{
			return Error{ErrorCode::InvalidArgument,
						 "cannot stash: the index has unresolved conflicts; resolve or discard the conflicts first"};
		}

		if (saveResult != 0)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot stash: {}", LastLibgit2Message())};
		}

		// The work is safe in the stash by now; a failure here leaves it on the tree as well.
		Result<void> cleared = DiscardAll(untrackedFiles);

		if (!cleared.HasValue())
		{
			return Error{ErrorCode::IoFailure,
						 std::format("the changes were stashed as stash@{{0}} but could not be cleared from "
									 "the working tree: {}",
									 cleared.GetError().Message)};
		}

		return true;
	}

	Result<std::vector<StashEntry>> Repository::QueryStashes() const
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		return ListStashes(m_Handle.get());
	}

	Result<void> Repository::StashApply(const Oid& id)
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		const Result<std::size_t> stashPosition = FindStashPosition(m_Handle.get(), id);

		if (!stashPosition.HasValue())
		{
			return stashPosition.GetError();
		}

		// git_stash_apply refuses over differences between HEAD and the cached index, and a stale one
		// reports a clean tree as having uncommitted changes.
		if (const Result<void> refreshed = RefreshIndex(m_Handle.get()); !refreshed.HasValue())
		{
			return refreshed.GetError();
		}

		git_stash_apply_options options = GIT_STASH_APPLY_OPTIONS_INIT;
		options.flags = GIT_STASH_APPLY_REINSTATE_INDEX;

		if (git_stash_apply(m_Handle.get(), stashPosition.GetValue(), &options) != 0)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot apply the stash: {}", LastLibgit2Message())};
		}

		const Result<bool> hasConflicts = IndexHasConflicts(m_Handle.get());

		if (!hasConflicts.HasValue())
		{
			return Error{ErrorCode::IoFailure, std::format("the stash was applied: {}", hasConflicts.GetError().Message)};
		}

		// Reported as an error although the stash applied: the Changes list alone would not tell the
		// reader where the new conflicts came from.
		if (hasConflicts.GetValue())
		{
			return Error{ErrorCode::InvalidArgument,
						 "the stash was applied with conflicts in the index; the stash is kept"};
		}

		return Result<void>::Ok();
	}

	Result<void> Repository::StashPop(const Oid& id)
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		const Result<std::size_t> stashPosition = FindStashPosition(m_Handle.get(), id);

		if (!stashPosition.HasValue())
		{
			return stashPosition.GetError();
		}

		if (const Result<void> refreshed = RefreshIndex(m_Handle.get()); !refreshed.HasValue())
		{
			return refreshed.GetError();
		}

		git_stash_apply_options options = GIT_STASH_APPLY_OPTIONS_INIT;
		options.flags = GIT_STASH_APPLY_REINSTATE_INDEX;

		// Applied and dropped here rather than by git_stash_pop, which drops a stash it applied with
		// conflicts. Until those are resolved the stash is the only copy of the work, so git keeps it.
		if (git_stash_apply(m_Handle.get(), stashPosition.GetValue(), &options) != 0)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot pop the stash: {}", LastLibgit2Message())};
		}

		const Result<bool> hasConflicts = IndexHasConflicts(m_Handle.get());

		if (!hasConflicts.HasValue())
		{
			return Error{ErrorCode::IoFailure,
						 std::format("the stash was applied but kept: {}", hasConflicts.GetError().Message)};
		}

		if (hasConflicts.GetValue())
		{
			return Error{ErrorCode::InvalidArgument,
						 std::format("the stash was applied with conflicts and kept as stash@{{{}}}; resolve the "
									 "conflicts, then drop it",
									 stashPosition.GetValue())};
		}

		if (git_stash_drop(m_Handle.get(), stashPosition.GetValue()) != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("the stash was applied but could not be dropped: {}", LastLibgit2Message())};
		}

		return Result<void>::Ok();
	}

	Result<void> Repository::StashDrop(const Oid& id)
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		const Result<std::size_t> stashPosition = FindStashPosition(m_Handle.get(), id);

		if (!stashPosition.HasValue())
		{
			return stashPosition.GetError();
		}

		if (git_stash_drop(m_Handle.get(), stashPosition.GetValue()) != 0)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot drop the stash: {}", LastLibgit2Message())};
		}

		return Result<void>::Ok();
	}
}
