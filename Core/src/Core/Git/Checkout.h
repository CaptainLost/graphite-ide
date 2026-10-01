#pragma once

#include "Core/Git/Oid.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Core {

	enum class CheckoutKind
	{
		LocalBranch,
		RemoteBranch,
		Commit
	};

	struct CheckoutTarget
	{
		CheckoutKind Kind = CheckoutKind::LocalBranch;

		// "feature/x" for a local branch, "origin/feature/y" for a remote one.
		std::string Name;

		// For CheckoutKind::Commit.
		Oid Commit;

		friend bool operator==(const CheckoutTarget&, const CheckoutTarget&) = default;
	};

	// What a checkout would do, found by a dry run that changes nothing. Paths are
	// relative to the working directory.
	struct CheckoutPlan
	{
		// Files with changes of their own that the checkout would have to overwrite - the
		// ones a real, safe checkout would refuse over.
		std::vector<std::filesystem::path> Overwritten;

		// Files the checkout would write or remove. An unsaved editor buffer on one of
		// these would end up over the other branch's text, which git cannot see.
		std::vector<std::filesystem::path> Updated;
	};
}
