#pragma once

#include "Core/Base/Result.h"
#include "Core/Git/Oid.h"

#include <filesystem>
#include <optional>
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

		// Read only when Kind is CheckoutKind::Commit.
		Oid Commit;

		friend bool operator==(const CheckoutTarget&, const CheckoutTarget&) = default;
	};

	// Paths are relative to the working directory.
	struct CheckoutPlan
	{
		// Files a safe checkout refuses to overwrite: those with changes of their own, and
		// every unresolved conflict in the index.
		std::vector<std::filesystem::path> Overwritten;

		// An unsaved editor buffer on one of these files would end up over the other
		// branch's text, which git cannot see.
		std::vector<std::filesystem::path> Updated;
	};

	// A refusal, or a failure before any file is written, leaves Changed empty. A failure
	// after writing began can leave the tree partly switched, and Changed then names every
	// path that may have changed.
	struct CheckoutOutcome
	{
		std::vector<std::filesystem::path> Changed;
		std::optional<Error> Failure;
	};
}
