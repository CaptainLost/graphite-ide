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

		struct Collected
		{
			git_repository* Handle = nullptr;
			std::vector<StashEntry> Entries;
			bool CommitUnreadable = false;
		};

		int Collect(std::size_t index, const char* message, const git_oid* id, void* payload)
		{
			auto& collected = *static_cast<Collected*>(payload);

			CommitHandle commit;

			// A stash whose commit cannot be read is reported, not listed with a zero time:
			// the list is what the writes resolve an id against, and a half-read entry would
			// make a present stash look absent or a stale one look current.
			if (git_commit_lookup(Out(commit), collected.Handle, id) != 0)
			{
				collected.CommitUnreadable = true;
				return -1;
			}

			StashEntry entry;
			entry.Index = index;
			entry.Message = message != nullptr ? message : "";
			entry.Id = ToOid(*id);
			entry.Time = static_cast<std::int64_t>(git_commit_time(commit.get()));

			collected.Entries.push_back(std::move(entry));
			return 0;
		}

		Result<std::vector<StashEntry>> ListStashes(git_repository* handle)
		{
			Collected collected{handle, {}, false};
			const int result = git_stash_foreach(handle, Collect, &collected);

			if (collected.CommitUnreadable)
				return Error{ErrorCode::IoFailure, std::format("cannot read a stash commit: {}", LastLibgit2Message())};

			if (result != 0)
				return Error{ErrorCode::IoFailure, std::format("cannot list stashes: {}", LastLibgit2Message())};

			return std::move(collected.Entries);
		}

		// The stash's position right now. An error from listing is passed on as itself rather
		// than turned into "not found": a stash that could not be looked for is not gone.
		Result<std::size_t> IndexOf(git_repository* handle, const Oid& id)
		{
			auto stashes = ListStashes(handle);

			if (!stashes.HasValue())
				return stashes.GetError();

			for (const StashEntry& entry : stashes.GetValue())
			{
				if (entry.Id == id)
					return entry.Index;
			}

			return Error{ErrorCode::NotFound, "that stash no longer exists"};
		}

		// Whether applying a stash left conflicts. The index is re-read first: libgit2 keeps
		// one per repository handle, and the apply wrote the file.
		Result<bool> HasConflicts(git_repository* handle)
		{
			IndexHandle index;

			// Two statements: Out hands the index over only at the end of the full expression.
			if (git_repository_index(Out(index), handle) != 0)
				return Error{ErrorCode::IoFailure, std::format("cannot read the index: {}", LastLibgit2Message())};

			if (git_index_read(index.get(), false) != 0)
				return Error{ErrorCode::IoFailure, std::format("cannot read the index: {}", LastLibgit2Message())};

			return git_index_has_conflicts(index.get()) != 0;
		}
	}

	Result<bool> Repository::StashSave(const std::string& message, bool includeUntracked)
	{
		if (m_Handle == nullptr)
			return Error{ErrorCode::NotFound, "the repository is not open"};

		SignatureHandle stasher;

		if (git_signature_default(Out(stasher), m_Handle) != 0)
		{
			return Error{ErrorCode::InvalidArgument,
						 std::format("cannot stash without a git identity: {}", LastLibgit2Message())};
		}

		// libgit2 records the stash and then clears the tree itself, but with untracked files
		// it clears through REMOVE_UNTRACKED, which deletes an untracked directory whole -
		// ignored files inside it included, though the stash never took them. So the stash
		// only records, and the tree is cleared by DiscardAll, which removes untracked files
		// one by one and leaves ignored ones where they are.
		git_oid id{};
		const std::uint32_t flags =
			GIT_STASH_KEEP_ALL | (includeUntracked ? GIT_STASH_INCLUDE_UNTRACKED : GIT_STASH_DEFAULT);
		const int result = git_stash_save(&id, m_Handle, stasher.get(),
										  message.empty() ? nullptr : message.c_str(), flags);

		if (result == GIT_ENOTFOUND)
			return false;

		// libgit2 cannot write a tree from a conflicted index, and says so in its own terms;
		// it refuses before recording anything.
		if (result == GIT_EUNMERGED)
		{
			return Error{ErrorCode::InvalidArgument,
						 "cannot stash: the index has unresolved conflicts; resolve or discard the conflicts first"};
		}

		if (result != 0)
			return Error{ErrorCode::IoFailure, std::format("cannot stash: {}", LastLibgit2Message())};

		// The work is safe in the stash by now; a failure here leaves it on the tree as well.
		auto cleared = DiscardAll(includeUntracked);

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
			return Error{ErrorCode::NotFound, "the repository is not open"};

		return ListStashes(m_Handle);
	}

	Result<void> Repository::StashApply(const Oid& id)
	{
		if (m_Handle == nullptr)
			return Error{ErrorCode::NotFound, "the repository is not open"};

		const auto index = IndexOf(m_Handle, id);

		if (!index.HasValue())
			return index.GetError();

		git_stash_apply_options options = GIT_STASH_APPLY_OPTIONS_INIT;
		options.flags = GIT_STASH_APPLY_REINSTATE_INDEX;

		if (git_stash_apply(m_Handle, index.GetValue(), &options) != 0)
			return Error{ErrorCode::IoFailure, std::format("cannot apply the stash: {}", LastLibgit2Message())};

		// Reported as a success by libgit2 - see StashPop - but the reader has conflicts to
		// resolve now, and the Changes list alone does not say where they came from.
		const auto conflicted = HasConflicts(m_Handle);

		if (!conflicted.HasValue())
			return Error{ErrorCode::IoFailure, std::format("the stash was applied: {}", conflicted.GetError().Message)};

		if (conflicted.GetValue())
		{
			return Error{ErrorCode::InvalidArgument,
						 "the stash was applied with conflicts in the index; the stash is kept"};
		}

		return Result<void>::Ok();
	}

	Result<void> Repository::StashPop(const Oid& id)
	{
		if (m_Handle == nullptr)
			return Error{ErrorCode::NotFound, "the repository is not open"};

		const auto index = IndexOf(m_Handle, id);

		if (!index.HasValue())
			return index.GetError();

		git_stash_apply_options options = GIT_STASH_APPLY_OPTIONS_INIT;
		options.flags = GIT_STASH_APPLY_REINSTATE_INDEX;

		// Applied and dropped here rather than by git_stash_pop. A stash that collides with a
		// local change is refused before anything is written, but one that collides with the
		// branch is applied with the conflict left in the index and reported as a success -
		// and git_stash_pop then drops it. git keeps the stash in that case, and so does this:
		// it is the only copy of the work until the conflict is resolved.
		if (git_stash_apply(m_Handle, index.GetValue(), &options) != 0)
			return Error{ErrorCode::IoFailure, std::format("cannot pop the stash: {}", LastLibgit2Message())};

		const auto conflicted = HasConflicts(m_Handle);

		if (!conflicted.HasValue())
		{
			return Error{ErrorCode::IoFailure,
						 std::format("the stash was applied but kept: {}", conflicted.GetError().Message)};
		}

		if (conflicted.GetValue())
		{
			return Error{ErrorCode::InvalidArgument,
						 std::format("the stash was applied with conflicts and kept as stash@{{{}}}; resolve the "
									 "conflicts, then drop it",
									 index.GetValue())};
		}

		if (git_stash_drop(m_Handle, index.GetValue()) != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("the stash was applied but could not be dropped: {}", LastLibgit2Message())};
		}

		return Result<void>::Ok();
	}

	Result<void> Repository::StashDrop(const Oid& id)
	{
		if (m_Handle == nullptr)
			return Error{ErrorCode::NotFound, "the repository is not open"};

		const auto index = IndexOf(m_Handle, id);

		if (!index.HasValue())
			return index.GetError();

		if (git_stash_drop(m_Handle, index.GetValue()) != 0)
			return Error{ErrorCode::IoFailure, std::format("cannot drop the stash: {}", LastLibgit2Message())};

		return Result<void>::Ok();
	}
}
