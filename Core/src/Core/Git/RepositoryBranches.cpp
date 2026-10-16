#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/GitHandles.h"
#include "Core/Git/Libgit2Support.h"

#include <git2.h>

#include <cstring>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>

namespace Core {

	namespace {

		constexpr int kBranchNameIsValid = 1;
		constexpr int kBranchIsCheckedOut = 1;

		Result<ReferenceHandle> LookupLocalBranch(git_repository* handle, const std::string& name)
		{
			ReferenceHandle branch;

			if (git_branch_lookup(Out(branch), handle, name.c_str(), GIT_BRANCH_LOCAL) != 0)
			{
				return Error{ErrorCode::NotFound,
							 std::format("no branch named '{}': {}", name, LastLibgit2Message())};
			}

			return branch;
		}

		// Whether a linked worktree whose folder is gone still counts. Its HEAD stays on record in the
		// main repository's `worktrees` directory until `git worktree prune` runs.
		enum class GoneWorktrees
		{
			// Counted like any other, as the refusal to delete a checked-out branch always has.
			Counted,

			// Left out: there is no folder to read the branch's work from.
			Skipped
		};

		// Empty when the branch is checked out in this repository's own working tree, or when the
		// lookup fails; the caller's plain refusal is still true then.
		std::string LinkedWorktreeHoldingBranch(git_repository* handle, const char* branchReference,
												GoneWorktrees goneWorktrees)
		{
			git_strarray worktreeNames{};

			if (git_worktree_list(&worktreeNames, handle) != 0)
			{
				return std::string{};
			}

			std::string worktreePath;

			for (std::size_t worktreeIndex = 0; worktreeIndex < worktreeNames.count && worktreePath.empty();
				 ++worktreeIndex)
			{
				const char* worktreeName = worktreeNames.strings[worktreeIndex];
				ReferenceHandle worktreeHead;

				// The handle takes the reference only when the call's full expression ends, so it is
				// read in a second statement, never in the same condition.
				const int headLookupResult = git_repository_head_for_worktree(Out(worktreeHead), handle, worktreeName);

				if (headLookupResult != 0)
				{
					continue;
				}

				const bool headIsThisBranch = std::strcmp(git_reference_name(worktreeHead.get()), branchReference) == 0;

				if (!headIsThisBranch)
				{
					continue;
				}

				WorktreeHandle worktree;

				if (git_worktree_lookup(Out(worktree), handle, worktreeName) != 0)
				{
					continue;
				}

				const bool skippedAsGone =
					goneWorktrees == GoneWorktrees::Skipped && git_worktree_validate(worktree.get()) != 0;

				if (skippedAsGone)
				{
					continue;
				}

				worktreePath = git_worktree_path(worktree.get());
			}

			git_strarray_dispose(&worktreeNames);
			return worktreePath;
		}

		constexpr int kRepositoryIsLinkedWorktree = 1;

		using RepositoryHandle = std::unique_ptr<git_repository, decltype(&git_repository_free)>;

		// A detached or unborn HEAD has no branch's name, so it reads as holding none.
		bool HeadIsBranch(git_repository* handle, const std::string& branchFullName)
		{
			ReferenceHandle head;

			// The handle takes the reference only when the call's full expression ends, so it is
			// read in a second statement, never in the same condition.
			const int headLookupResult = git_repository_head(Out(head), handle);

			return headLookupResult == 0 && branchFullName == git_reference_name(head.get());
		}

		std::optional<std::filesystem::path> MainCheckoutHolding(git_repository* linkedWorktree,
																 const std::string& branchFullName)
		{
			git_repository* opened = nullptr;

			if (git_repository_open(&opened, git_repository_commondir(linkedWorktree)) != 0)
			{
				return std::nullopt;
			}

			const RepositoryHandle mainRepository{opened, &git_repository_free};
			const char* mainWorkingDirectory = git_repository_workdir(mainRepository.get());

			if (mainWorkingDirectory == nullptr || !HeadIsBranch(mainRepository.get(), branchFullName))
			{
				return std::nullopt;
			}

			return FromUtf8(mainWorkingDirectory);
		}
	}

	bool Repository::IsValidBranchName(const std::string& name)
	{
		EnsureLibgit2Initialised();

		if (name.empty())
		{
			return false;
		}

		int validFlag = 0;

		if (git_branch_name_is_valid(&validFlag, name.c_str()) != 0)
		{
			return false;
		}

		return validFlag == kBranchNameIsValid;
	}

	Result<std::optional<std::filesystem::path>> Repository::WorktreeHolding(const std::string& branchFullName) const
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		if (HeadIsBranch(m_Handle.get(), branchFullName))
		{
			return std::optional<std::filesystem::path>{m_WorkingDirectory};
		}

		const std::string linkedWorktree =
			LinkedWorktreeHoldingBranch(m_Handle.get(), branchFullName.c_str(), GoneWorktrees::Skipped);

		if (!linkedWorktree.empty())
		{
			return std::optional<std::filesystem::path>{FromUtf8(linkedWorktree)};
		}

		if (git_repository_is_worktree(m_Handle.get()) == kRepositoryIsLinkedWorktree)
		{
			return MainCheckoutHolding(m_Handle.get(), branchFullName);
		}

		return std::optional<std::filesystem::path>{};
	}

	Result<void> Repository::CreateBranch(const std::string& name, const Oid& from)
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		Result<CommitHandle> lookedUp = LookupCommit(m_Handle.get(), from);

		if (!lookedUp.HasValue())
		{
			return lookedUp.GetError();
		}

		const CommitHandle commit = std::move(lookedUp).GetValue();

		ReferenceHandle createdBranch;
		const int failIfTheBranchExists = 0;
		const int createResult =
			git_branch_create(Out(createdBranch), m_Handle.get(), name.c_str(), commit.get(), failIfTheBranchExists);

		if (createResult != 0)
		{
			return Error{ErrorCode::InvalidArgument,
						 std::format("cannot create branch '{}': {}", name, LastLibgit2Message())};
		}

		return Result<void>::Ok();
	}

	Result<void> Repository::RenameBranch(const std::string& from, const std::string& to)
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		Result<ReferenceHandle> branchToRename = LookupLocalBranch(m_Handle.get(), from);

		if (!branchToRename.HasValue())
		{
			return branchToRename.GetError();
		}

		ReferenceHandle renamedBranch;
		const int failIfTheNameIsTaken = 0;
		const int moveResult = git_branch_move(Out(renamedBranch), branchToRename.GetValue().get(), to.c_str(),
											   failIfTheNameIsTaken);

		if (moveResult != 0)
		{
			return Error{ErrorCode::InvalidArgument,
						 std::format("cannot rename '{}' to '{}': {}", from, to, LastLibgit2Message())};
		}

		return Result<void>::Ok();
	}

	Result<std::size_t> Repository::CountUnmergedCommits(const std::string& name) const
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		RevwalkHandle revisionWalk;

		if (git_revwalk_new(Out(revisionWalk), m_Handle.get()) != 0)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot walk: {}", LastLibgit2Message())};
		}

		const std::string branchReference = LocalBranchReference(name);

		if (git_revwalk_push_ref(revisionWalk.get(), branchReference.c_str()) != 0)
		{
			return Error{ErrorCode::NotFound, std::format("no branch named '{}'", name)};
		}

		BranchIteratorHandle everyBranch;

		if (git_branch_iterator_new(Out(everyBranch), m_Handle.get(), GIT_BRANCH_ALL) == 0)
		{
			git_branch_t branchType = GIT_BRANCH_LOCAL;

			while (true)
			{
				ReferenceHandle branch;

				if (git_branch_next(Out(branch), &branchType, everyBranch.get()) != 0)
				{
					break;
				}

				const char* branchName = git_reference_name(branch.get());
				const bool isAnotherBranch = branchName != nullptr && branchReference != branchName;

				if (!isAnotherBranch)
				{
					continue;
				}

				const git_oid* otherBranchTip = git_reference_target(branch.get());

				if (otherBranchTip != nullptr)
				{
					git_revwalk_hide(revisionWalk.get(), otherBranchTip);
				}
			}
		}

		std::size_t unmergedCount = 0;
		git_oid walkedId{};

		while (git_revwalk_next(&walkedId, revisionWalk.get()) == 0)
		{
			++unmergedCount;
		}

		return unmergedCount;
	}

	Result<void> Repository::DeleteBranch(const std::string& name)
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		Result<ReferenceHandle> branchToDelete = LookupLocalBranch(m_Handle.get(), name);

		if (!branchToDelete.HasValue())
		{
			return branchToDelete.GetError();
		}

		git_reference* branch = branchToDelete.GetValue().get();
		const int checkedOutResult = git_branch_is_checked_out(branch);

		if (checkedOutResult < 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot tell whether '{}' is checked out: {}", name, LastLibgit2Message())};
		}

		if (checkedOutResult == kBranchIsCheckedOut)
		{
			const std::string holdingWorktree =
				LinkedWorktreeHoldingBranch(m_Handle.get(), git_reference_name(branch), GoneWorktrees::Counted);

			if (!holdingWorktree.empty())
			{
				return Error{ErrorCode::InvalidArgument, std::format("'{}' is checked out in {}", name, holdingWorktree)};
			}

			return Error{ErrorCode::InvalidArgument,
						 std::format("'{}' is checked out and cannot be deleted", name)};
		}

		const int deleteResult = git_branch_delete(branch);

		if (deleteResult != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot delete '{}': {}", name, LastLibgit2Message())};
		}

		return Result<void>::Ok();
	}
}
