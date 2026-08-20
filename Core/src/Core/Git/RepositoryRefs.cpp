#include "corepch.h"

#include "Core/Git/Repository.h"

#include <git2.h>

#include <algorithm>
#include <format>
#include <string>

namespace Core {

	namespace {

		std::string LastMessage()
		{
			const git_error* detail = git_error_last();

			return detail != nullptr && detail->message != nullptr ? detail->message
																   : "unknown error";
		}

		Oid ToOid(const git_oid& id)
		{
			Oid result;

			for (std::size_t index = 0; index < result.Bytes.size(); ++index)
			{
				result.Bytes[index] = static_cast<std::uint8_t>(id.id[index]);
			}

			return result;
		}
	}

	Result<std::vector<Branch>> Repository::QueryBranches() const
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		git_branch_iterator* iterator = nullptr;

		if (git_branch_iterator_new(&iterator, m_Handle, GIT_BRANCH_ALL) != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot list the branches: {}", LastMessage())};
		}

		std::vector<Branch> branches;

		git_reference* reference = nullptr;
		git_branch_t type = GIT_BRANCH_LOCAL;

		while (git_branch_next(&reference, &type, iterator) == 0)
		{
			Branch branch;
			branch.Kind = type == GIT_BRANCH_REMOTE ? RefKind::Remote : RefKind::Local;

			if (const char* name = nullptr; git_branch_name(&name, reference) == 0
										   && name != nullptr)
			{
				branch.Name = name;
			}

			if (const char* full = git_reference_name(reference); full != nullptr)
			{
				branch.FullName = full;
			}

			// A symbolic ref such as refs/remotes/origin/HEAD has no direct target. It
			// names another branch rather than a commit, so there is nothing to show.
			const git_oid* target = git_reference_target(reference);

			if (target == nullptr)
			{
				git_reference_free(reference);
				continue;
			}

			branch.Tip = ToOid(*target);
			branch.IsHead = git_branch_is_head(reference) == 1;

			git_reference* upstream = nullptr;

			if (git_branch_upstream(&upstream, reference) == 0 && upstream != nullptr)
			{
				if (const char* name = nullptr; git_branch_name(&name, upstream) == 0
											   && name != nullptr)
				{
					branch.Upstream = name;
				}

				if (const git_oid* upstreamTarget = git_reference_target(upstream);
					upstreamTarget != nullptr)
				{
					std::size_t ahead = 0;
					std::size_t behind = 0;

					if (git_graph_ahead_behind(&ahead, &behind, m_Handle, target, upstreamTarget)
						== 0)
					{
						branch.Ahead = ahead;
						branch.Behind = behind;
					}
				}

				git_reference_free(upstream);
			}

			branches.push_back(std::move(branch));
			git_reference_free(reference);
		}

		git_branch_iterator_free(iterator);

		std::sort(branches.begin(), branches.end(), [](const Branch& left, const Branch& right) {
			if (left.Kind != right.Kind)
			{
				return left.Kind == RefKind::Local;
			}

			return left.Name < right.Name;
		});

		return branches;
	}
}
