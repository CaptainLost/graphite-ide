#pragma once

#include "Core/Git/Oid.h"

#include <optional>
#include <string>

namespace Core {

	enum class HeadKind
	{
		// HEAD names a branch that has at least one commit.
		Branch,

		// HEAD names a commit directly.
		Detached,

		// HEAD names a branch that does not exist yet: a repository with no commits.
		Unborn
	};

	// An operation git has started and not finished. Folded from libgit2's finer states:
	// every flavour of rebase is a rebase to the person reading the bar.
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

		// "main" for Branch and Unborn; empty for Detached.
		std::string BranchName;

		// The commit HEAD points at; unset for Unborn.
		std::optional<Oid> Commit;

		RepositoryOperation Operation = RepositoryOperation::None;
	};
}
