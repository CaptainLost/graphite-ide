#pragma once

#include "Core/Git/Oid.h"

#include <optional>
#include <string>

namespace Core {

	enum class HeadKind
	{
		Branch,
		Detached,

		// HEAD names a branch that has no commit yet, as in a new repository.
		Unborn
	};

	// An operation git has started and not finished, folded from libgit2's finer states:
	// every flavour of rebase is a Rebase.
	enum class RepositoryOperation
	{
		None,
		Merge,
		Rebase,
		CherryPick,
		Revert,
		Bisect,
		ApplyMailbox
	};

	struct HeadState
	{
		HeadKind Kind = HeadKind::Branch;

		// Empty for Detached.
		std::string BranchName;

		// Unset for Unborn.
		std::optional<Oid> Commit;

		RepositoryOperation Operation = RepositoryOperation::None;

		friend bool operator==(const HeadState&, const HeadState&) = default;
	};
}
