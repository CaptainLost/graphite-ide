#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/Git/Libgit2Support.h"

#include <git2.h>

#include <format>
#include <string>

namespace Core {

	namespace {

		Result<git_reference*> LookupLocal(git_repository* handle, const std::string& name)
		{
			git_reference* reference = nullptr;

			if (git_branch_lookup(&reference, handle, name.c_str(), GIT_BRANCH_LOCAL) != 0)
			{
				return Error{ErrorCode::NotFound,
							 std::format("no branch named '{}': {}", name, LastLibgit2Message())};
			}

			return reference;
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
			return Error{ErrorCode::NotFound, "the repository is not open"};

		const git_oid id = ToGitOid(from);
		git_commit* commit = nullptr;

		if (git_commit_lookup(&commit, m_Handle, &id) != 0)
		{
			return Error{ErrorCode::NotFound,
						 std::format("no commit {}: {}", from.ShortHex(), LastLibgit2Message())};
		}

		git_reference* created = nullptr;
		const int result = git_branch_create(&created, m_Handle, name.c_str(), commit, 0);
		git_commit_free(commit);

		if (result != 0)
		{
			return Error{ErrorCode::InvalidArgument,
						 std::format("cannot create branch '{}': {}", name, LastLibgit2Message())};
		}

		git_reference_free(created);
		return Result<void>::Ok();
	}

	Result<void> Repository::RenameBranch(const std::string& from, const std::string& to)
	{
		if (m_Handle == nullptr)
			return Error{ErrorCode::NotFound, "the repository is not open"};

		auto reference = LookupLocal(m_Handle, from);

		if (!reference.HasValue())
			return reference.GetError();

		git_reference* moved = nullptr;
		const int result = git_branch_move(&moved, reference.GetValue(), to.c_str(), 0);
		git_reference_free(reference.GetValue());

		if (result != 0)
		{
			return Error{ErrorCode::InvalidArgument,
						 std::format("cannot rename '{}' to '{}': {}", from, to, LastLibgit2Message())};
		}

		git_reference_free(moved);
		return Result<void>::Ok();
	}

	Result<std::size_t> Repository::CountUnmergedCommits(const std::string& name) const
	{
		if (m_Handle == nullptr)
			return Error{ErrorCode::NotFound, "the repository is not open"};

		git_revwalk* walk = nullptr;

		if (git_revwalk_new(&walk, m_Handle) != 0)
			return Error{ErrorCode::IoFailure, std::format("cannot walk: {}", LastLibgit2Message())};

		const std::string full = "refs/heads/" + name;

		if (git_revwalk_push_ref(walk, full.c_str()) != 0)
		{
			git_revwalk_free(walk);
			return Error{ErrorCode::NotFound, std::format("no branch named '{}'", name)};
		}

		// Every other branch, local and remote, is hidden: what remains is reachable only
		// from this one.
		git_branch_iterator* iterator = nullptr;

		if (git_branch_iterator_new(&iterator, m_Handle, GIT_BRANCH_ALL) == 0)
		{
			git_reference* reference = nullptr;
			git_branch_t type = GIT_BRANCH_LOCAL;

			while (git_branch_next(&reference, &type, iterator) == 0)
			{
				const char* other = git_reference_name(reference);

				if (other != nullptr && full != other && git_reference_target(reference) != nullptr)
					git_revwalk_hide(walk, git_reference_target(reference));

				git_reference_free(reference);
			}

			git_branch_iterator_free(iterator);
		}

		std::size_t count = 0;
		git_oid id{};

		while (git_revwalk_next(&id, walk) == 0)
			++count;

		git_revwalk_free(walk);
		return count;
	}

	Result<void> Repository::DeleteBranch(const std::string& name)
	{
		if (m_Handle == nullptr)
			return Error{ErrorCode::NotFound, "the repository is not open"};

		auto reference = LookupLocal(m_Handle, name);

		if (!reference.HasValue())
			return reference.GetError();

		const int checkedOut = git_branch_is_checked_out(reference.GetValue());

		if (checkedOut < 0)
		{
			git_reference_free(reference.GetValue());
			return Error{ErrorCode::IoFailure,
						 std::format("cannot tell whether '{}' is checked out: {}", name, LastLibgit2Message())};
		}

		if (checkedOut == 1)
		{
			git_reference_free(reference.GetValue());
			return Error{ErrorCode::InvalidArgument,
						 std::format("'{}' is checked out and cannot be deleted", name)};
		}

		const int result = git_branch_delete(reference.GetValue());
		git_reference_free(reference.GetValue());

		if (result != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot delete '{}': {}", name, LastLibgit2Message())};
		}

		return Result<void>::Ok();
	}
}
