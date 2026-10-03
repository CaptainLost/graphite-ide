#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/Git/GitHandles.h"
#include "Core/Git/Libgit2Support.h"

#include <git2.h>

#include <algorithm>
#include <format>
#include <string>
#include <string_view>

namespace Core {

	namespace {

		RepositoryOperation OperationOf(int state)
		{
			switch (state)
			{
			case GIT_REPOSITORY_STATE_MERGE:
				return RepositoryOperation::Merge;

			case GIT_REPOSITORY_STATE_REVERT:
			case GIT_REPOSITORY_STATE_REVERT_SEQUENCE:
				return RepositoryOperation::Revert;

			case GIT_REPOSITORY_STATE_CHERRYPICK:
			case GIT_REPOSITORY_STATE_CHERRYPICK_SEQUENCE:
				return RepositoryOperation::CherryPick;

			case GIT_REPOSITORY_STATE_BISECT:
				return RepositoryOperation::Bisect;

			case GIT_REPOSITORY_STATE_REBASE:
			case GIT_REPOSITORY_STATE_REBASE_INTERACTIVE:
			case GIT_REPOSITORY_STATE_REBASE_MERGE:
			case GIT_REPOSITORY_STATE_APPLY_MAILBOX_OR_REBASE:
				return RepositoryOperation::Rebase;

			case GIT_REPOSITORY_STATE_APPLY_MAILBOX:
				return RepositoryOperation::ApplyMailbox;

			default:
				return RepositoryOperation::None;
			}
		}

		// "refs/heads/main" to "main". An unborn HEAD is only a symbolic name, so there is
		// no branch object to ask git_branch_name about.
		std::string ShortBranchName(std::string_view full)
		{
			constexpr std::string_view prefix = "refs/heads/";

			if (full.starts_with(prefix))
				full.remove_prefix(prefix.size());

			return std::string{full};
		}
	}

	Result<std::vector<Branch>> Repository::QueryBranches() const
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		BranchIteratorHandle iterator;

		if (git_branch_iterator_new(Out(iterator), m_Handle.get(), GIT_BRANCH_ALL) != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot list the branches: {}", LastLibgit2Message())};
		}

		std::vector<Branch> branches;

		git_branch_t type = GIT_BRANCH_LOCAL;

		int next = 0;

		while (true)
		{
			ReferenceHandle reference;
			next = git_branch_next(Out(reference), &type, iterator.get());

			if (next != 0)
			{
				break;
			}

			Branch branch;
			branch.Kind = type == GIT_BRANCH_REMOTE ? RefKind::Remote : RefKind::Local;

			if (const char* name = nullptr; git_branch_name(&name, reference.get()) == 0 && name != nullptr)
			{
				branch.Name = name;
			}

			if (const char* full = git_reference_name(reference.get()); full != nullptr)
			{
				branch.FullName = full;
			}

			// A symbolic ref such as refs/remotes/origin/HEAD has no direct target. It
			// names another branch rather than a commit, so there is nothing to show.
			const git_oid* target = git_reference_target(reference.get());

			if (target == nullptr)
			{
				continue;
			}

			branch.Tip = ToOid(*target);
			branch.IsHead = git_branch_is_head(reference.get()) == 1;

			ReferenceHandle upstream;

			const int tracked = git_branch_upstream(Out(upstream), reference.get());

			if (tracked == 0 && upstream != nullptr)
			{
				if (const char* name = nullptr; git_branch_name(&name, upstream.get()) == 0 && name != nullptr)
				{
					branch.Upstream = name;
				}

				if (const git_oid* upstreamTarget = git_reference_target(upstream.get());
					upstreamTarget != nullptr)
				{
					std::size_t ahead = 0;
					std::size_t behind = 0;

					if (git_graph_ahead_behind(&ahead, &behind, m_Handle.get(), target, upstreamTarget) == 0)
					{
						branch.Ahead = ahead;
						branch.Behind = behind;
						branch.DistanceKnown = true;
					}
				}
			}

			branches.push_back(std::move(branch));
		}

		// GIT_ITEROVER is the ordinary end. Anything else means the listing stopped
		// partway, and a partial list must not pass for the whole one.
		if (next != GIT_ITEROVER)
		{
			const std::string message = LastLibgit2Message();
			return Error{ErrorCode::IoFailure, std::format("cannot list the branches: {}", message)};
		}

		std::sort(branches.begin(), branches.end(), [](const Branch& left, const Branch& right) {
			if (left.Kind != right.Kind)
			{
				return left.Kind == RefKind::Local;
			}

			return left.Name < right.Name;
		});

		return branches;
	}

	Result<HeadState> Repository::QueryHead() const
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		HeadState head;
		head.Operation = OperationOf(git_repository_state(m_Handle.get()));

		const int unborn = git_repository_head_unborn(m_Handle.get());

		if (unborn < 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot read HEAD: {}", LastLibgit2Message())};
		}

		if (unborn == 1)
		{
			head.Kind = HeadKind::Unborn;

			ReferenceHandle symbolic;

			if (git_reference_lookup(Out(symbolic), m_Handle.get(), "HEAD") == 0)
			{
				if (const char* target = git_reference_symbolic_target(symbolic.get()); target != nullptr)
				{
					head.BranchName = ShortBranchName(target);
				}
			}

			return head;
		}

		ReferenceHandle reference;

		if (git_repository_head(Out(reference), m_Handle.get()) != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot read HEAD: {}", LastLibgit2Message())};
		}

		if (const git_oid* target = git_reference_target(reference.get()); target != nullptr)
		{
			head.Commit = ToOid(*target);
		}

		if (git_repository_head_detached(m_Handle.get()) == 1)
		{
			head.Kind = HeadKind::Detached;
		}
		else
		{
			head.Kind = HeadKind::Branch;
			head.BranchName = git_reference_shorthand(reference.get());
		}

		return head;
	}
}
