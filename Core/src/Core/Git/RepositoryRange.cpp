#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/Git/GitHandles.h"
#include "Core/Git/Libgit2Support.h"

#include <git2.h>

#include <format>
#include <string>

namespace Core {

	Result<Oid> Repository::ResolveReference(const std::string& fullName) const
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		git_oid referenceTarget{};
		const int lookupResult = git_reference_name_to_id(&referenceTarget, m_Handle.get(), fullName.c_str());

		if (lookupResult == GIT_ENOTFOUND)
		{
			return Error{ErrorCode::NotFound, std::format("no reference named '{}'", fullName)};
		}

		if (lookupResult != 0)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot read '{}': {}", fullName, LastLibgit2Message())};
		}

		return ToOid(referenceTarget);
	}

	Result<Oid> Repository::MergeBase(const Oid& one, const Oid& other) const
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		const git_oid oneId = ToGitOid(one);
		const git_oid otherId = ToGitOid(other);
		git_oid mergeBaseId{};

		const int mergeBaseResult = git_merge_base(&mergeBaseId, m_Handle.get(), &oneId, &otherId);

		if (mergeBaseResult == GIT_ENOTFOUND)
		{
			return Error{ErrorCode::NotFound,
						 std::format("{} and {} have no common ancestor", one.ShortHex(), other.ShortHex())};
		}

		if (mergeBaseResult != 0)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot find the merge base of {} and {}: {}",
														   one.ShortHex(), other.ShortHex(), LastLibgit2Message())};
		}

		return ToOid(mergeBaseId);
	}
}
