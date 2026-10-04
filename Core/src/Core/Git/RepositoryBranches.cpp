#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/Git/GitHandles.h"
#include "Core/Git/Libgit2Support.h"

#include <git2.h>

#include <cstring>
#include <format>
#include <string>

namespace Core {

	namespace {

		Result<ReferenceHandle> LookupLocal(git_repository* handle, const std::string& name)
		{
			ReferenceHandle reference;

			if (git_branch_lookup(Out(reference), handle, name.c_str(), GIT_BRANCH_LOCAL) != 0)
			{
				return Error{ErrorCode::NotFound,
							 std::format("no branch named '{}': {}", name, LastLibgit2Message())};
			}

			return reference;
		}

		// The linked worktree whose HEAD is this branch, by path. Empty when it is checked out
		// in this repository's own working tree, or when the lookup fails - the caller then
		// keeps the plain refusal, which is still true.
		std::string WorktreeHolding(git_repository* handle, const char* referenceName)
		{
			git_strarray names{};

			if (git_worktree_list(&names, handle) != 0)
			{
				return {};
			}

			std::string path;

			for (std::size_t i = 0; i < names.count && path.empty(); ++i)
			{
				ReferenceHandle head;

				// The handle takes the reference only when the call's full expression ends, so it
				// is read in a second statement, never in the same condition.
				const int found = git_repository_head_for_worktree(Out(head), handle, names.strings[i]);

				if (found != 0 || std::strcmp(git_reference_name(head.get()), referenceName) != 0)
				{
					continue;
				}

				WorktreeHandle worktree;

				if (git_worktree_lookup(Out(worktree), handle, names.strings[i]) == 0)
				{
					path = git_worktree_path(worktree.get());
				}
			}

			git_strarray_dispose(&names);
			return path;
		}
	}

	bool Repository::IsValidBranchName(const std::string& name)
	{
		EnsureLibgit2Initialised();

		int valid = 0;
		return !name.empty() && git_branch_name_is_valid(&valid, name.c_str()) == 0 && valid == 1;
	}

	Result<void> Repository::CreateBranch(const std::string& name, const Oid& from)
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		const git_oid id = ToGitOid(from);
		CommitHandle commit;

		if (git_commit_lookup(Out(commit), m_Handle.get(), &id) != 0)
		{
			return Error{ErrorCode::NotFound,
						 std::format("no commit {}: {}", from.ShortHex(), LastLibgit2Message())};
		}

		ReferenceHandle created;
		const int result = git_branch_create(Out(created), m_Handle.get(), name.c_str(), commit.get(), 0);

		if (result != 0)
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
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		auto reference = LookupLocal(m_Handle.get(), from);

		if (!reference.HasValue())
		{
			return reference.GetError();
		}

		ReferenceHandle moved;
		const int result = git_branch_move(Out(moved), reference.GetValue().get(), to.c_str(), 0);

		if (result != 0)
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
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		RevwalkHandle walk;

		if (git_revwalk_new(Out(walk), m_Handle.get()) != 0)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot walk: {}", LastLibgit2Message())};
		}

		const std::string full = "refs/heads/" + name;

		if (git_revwalk_push_ref(walk.get(), full.c_str()) != 0)
		{
			return Error{ErrorCode::NotFound, std::format("no branch named '{}'", name)};
		}

		// Every other branch, local and remote, is hidden: what remains is reachable only
		// from this one.
		BranchIteratorHandle iterator;

		if (git_branch_iterator_new(Out(iterator), m_Handle.get(), GIT_BRANCH_ALL) == 0)
		{
			git_branch_t type = GIT_BRANCH_LOCAL;

			while (true)
			{
				ReferenceHandle reference;

				if (git_branch_next(Out(reference), &type, iterator.get()) != 0)
				{
					break;
				}

				const char* other = git_reference_name(reference.get());

				if (other != nullptr && full != other && git_reference_target(reference.get()) != nullptr)
				{
					git_revwalk_hide(walk.get(), git_reference_target(reference.get()));
				}
			}
		}

		std::size_t count = 0;
		git_oid id{};

		while (git_revwalk_next(&id, walk.get()) == 0)
		{
			++count;
		}

		return count;
	}

	Result<void> Repository::DeleteBranch(const std::string& name)
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		auto reference = LookupLocal(m_Handle.get(), name);

		if (!reference.HasValue())
		{
			return reference.GetError();
		}

		const int checkedOut = git_branch_is_checked_out(reference.GetValue().get());

		if (checkedOut < 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot tell whether '{}' is checked out: {}", name, LastLibgit2Message())};
		}

		if (checkedOut == 1)
		{
			if (const std::string worktree = WorktreeHolding(m_Handle.get(), git_reference_name(reference.GetValue().get()));
				!worktree.empty())
			{
				return Error{ErrorCode::InvalidArgument, std::format("'{}' is checked out in {}", name, worktree)};
			}

			return Error{ErrorCode::InvalidArgument,
						 std::format("'{}' is checked out and cannot be deleted", name)};
		}

		const int result = git_branch_delete(reference.GetValue().get());

		if (result != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot delete '{}': {}", name, LastLibgit2Message())};
		}

		return Result<void>::Ok();
	}
}
