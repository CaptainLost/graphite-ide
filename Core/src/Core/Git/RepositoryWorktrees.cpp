#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/GitHandles.h"
#include "Core/Git/Libgit2Support.h"

#include <git2.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Core {

	namespace {

		constexpr int kRepositoryIsLinkedWorktree = 1;

		using RepositoryHandle = std::unique_ptr<git_repository, decltype(&git_repository_free)>;

		// The repository whose worktree list is asked: the main one, reached through the common
		// directory when `handle` is itself a linked worktree.
		Result<RepositoryHandle> OpenMainRepository(git_repository* handle)
		{
			git_repository* opened = nullptr;

			if (git_repository_open(&opened, git_repository_commondir(handle)) != 0)
			{
				return Error{ErrorCode::IoFailure,
							 std::format("cannot open the main repository: {}", LastLibgit2Message())};
			}

			return RepositoryHandle{opened, &git_repository_free};
		}

		WorktreeEntry ReadLinkedWorktree(git_repository* mainHandle, const std::string& name)
		{
			WorktreeEntry entry;
			entry.Name = name;

			WorktreeHandle worktree;

			if (git_worktree_lookup(Out(worktree), mainHandle, name.c_str()) != 0)
			{
				entry.FolderMissing = true;
				return entry;
			}

			entry.Path = FromUtf8(git_worktree_path(worktree.get()));

			git_buf lockReason = GIT_BUF_INIT;
			entry.Locked = git_worktree_is_locked(&lockReason, worktree.get()) > 0;

			if (entry.Locked && lockReason.ptr != nullptr)
			{
				entry.LockReason = lockReason.ptr;
			}

			git_buf_dispose(&lockReason);

			if (git_worktree_validate(worktree.get()) != 0)
			{
				entry.FolderMissing = true;
				return entry;
			}

			Result<Repository> opened = Repository::OpenExactly(entry.Path);

			if (!opened.HasValue())
			{
				entry.FolderMissing = true;
				return entry;
			}

			Result<HeadState> head = opened.GetValue().QueryHead();

			if (!head.HasValue())
			{
				entry.FolderMissing = true;
				return entry;
			}

			entry.Head = std::move(head).GetValue();
			return entry;
		}
	}

	Result<std::vector<WorktreeEntry>> Repository::QueryWorktrees() const
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		const bool isLinkedWorktree = git_repository_is_worktree(m_Handle.get()) == kRepositoryIsLinkedWorktree;

		RepositoryHandle mainHandle{nullptr, &git_repository_free};

		if (isLinkedWorktree)
		{
			Result<RepositoryHandle> opened = OpenMainRepository(m_Handle.get());

			if (!opened.HasValue())
			{
				return opened.GetError();
			}

			mainHandle = std::move(opened).GetValue();
		}

		git_repository* listedFrom = isLinkedWorktree ? mainHandle.get() : m_Handle.get();

		WorktreeEntry mainEntry;
		mainEntry.IsMain = true;

		const char* mainWorkingDirectory = git_repository_workdir(listedFrom);
		mainEntry.Path = mainWorkingDirectory != nullptr ? FromUtf8(mainWorkingDirectory) : m_WorkingDirectory;

		if (isLinkedWorktree)
		{
			Result<Repository> mainRepository = Repository::OpenExactly(mainEntry.Path);

			if (!mainRepository.HasValue())
			{
				return mainRepository.GetError();
			}

			Result<HeadState> mainHead = mainRepository.GetValue().QueryHead();

			if (!mainHead.HasValue())
			{
				return mainHead.GetError();
			}

			mainEntry.Head = std::move(mainHead).GetValue();
		}
		else
		{
			Result<HeadState> mainHead = QueryHead();

			if (!mainHead.HasValue())
			{
				return mainHead.GetError();
			}

			mainEntry.Head = std::move(mainHead).GetValue();
		}

		git_strarray worktreeNames{};

		if (git_worktree_list(&worktreeNames, listedFrom) != 0)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot list worktrees: {}", LastLibgit2Message())};
		}

		std::vector<std::string> names;
		names.reserve(worktreeNames.count);

		for (std::size_t nameIndex = 0; nameIndex < worktreeNames.count; ++nameIndex)
		{
			names.emplace_back(worktreeNames.strings[nameIndex]);
		}

		git_strarray_dispose(&worktreeNames);
		std::sort(names.begin(), names.end());

		std::vector<WorktreeEntry> entries;
		entries.reserve(names.size() + 1);
		entries.push_back(std::move(mainEntry));

		for (const std::string& name : names)
		{
			entries.push_back(ReadLinkedWorktree(listedFrom, name));
		}

		return entries;
	}
}
