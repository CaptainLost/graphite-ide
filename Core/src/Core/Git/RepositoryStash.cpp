#include "corepch.h"

#include "Core/Git/Repository.h"

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

			git_commit* commit = nullptr;

			// A stash whose commit cannot be read is reported, not listed with a zero time:
			// the list is what the writes resolve an id against, and a half-read entry would
			// make a present stash look absent or a stale one look current.
			if (git_commit_lookup(&commit, collected.Handle, id) != 0)
			{
				collected.CommitUnreadable = true;
				return -1;
			}

			StashEntry entry;
			entry.Index = index;
			entry.Message = message != nullptr ? message : "";
			entry.Id = ToOid(*id);
			entry.Time = static_cast<std::int64_t>(git_commit_time(commit));
			git_commit_free(commit);

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
	}

	Result<bool> Repository::StashSave(const std::string& message, bool includeUntracked)
	{
		if (m_Handle == nullptr)
			return Error{ErrorCode::NotFound, "the repository is not open"};

		git_signature* stasher = nullptr;

		if (git_signature_default(&stasher, m_Handle) != 0)
		{
			return Error{ErrorCode::InvalidArgument,
						 std::format("cannot stash without a git identity: {}", LastLibgit2Message())};
		}

		git_oid id{};
		const std::uint32_t flags = includeUntracked ? GIT_STASH_INCLUDE_UNTRACKED : GIT_STASH_DEFAULT;
		const int result = git_stash_save(&id, m_Handle, stasher,
										  message.empty() ? nullptr : message.c_str(), flags);
		git_signature_free(stasher);

		if (result == GIT_ENOTFOUND)
			return false;

		if (result != 0)
			return Error{ErrorCode::IoFailure, std::format("cannot stash: {}", LastLibgit2Message())};

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

		// libgit2 keeps the stash when applying it fails, so a conflict loses nothing.
		if (git_stash_pop(m_Handle, index.GetValue(), &options) != 0)
			return Error{ErrorCode::IoFailure, std::format("cannot pop the stash: {}", LastLibgit2Message())};

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
