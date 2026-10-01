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

		// For CheckoutKind::Commit.
		Oid Commit;

		friend bool operator==(const CheckoutTarget&, const CheckoutTarget&) = default;
	};

	// What a checkout would do, found by a dry run that changes nothing. Paths are
	// relative to the working directory.
	struct CheckoutPlan
	{
		// Files with changes of their own that the checkout would have to overwrite - the
		// ones a real, safe checkout would refuse over. Unresolved conflicts in the index are
		// listed here too: a checkout refuses over every one of them.
		std::vector<std::filesystem::path> Overwritten;

		// Files the checkout would write or remove. An unsaved editor buffer on one of
		// these would end up over the other branch's text, which git cannot see.
		std::vector<std::filesystem::path> Updated;
	};

	// What a checkout did. A refusal - the target would overwrite changes, or the index has
	// unresolved conflicts - and any failure before the checkout starts changes nothing:
	// Failure is set and Changed is empty. A failure after files began to be written can
	// leave the working tree partly switched; Failure says so and Changed then names every
	// path that may have changed. On success Changed is what the checkout wrote or removed.
	struct CheckoutOutcome
	{
		std::vector<std::filesystem::path> Changed;
		std::optional<Error> Failure;
	};
}
