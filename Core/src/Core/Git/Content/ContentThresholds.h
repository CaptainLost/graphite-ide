#pragma once

#include <cstddef>

namespace Core {

	// The most commits of a line a patch-equivalence check walks for one branch.
	inline constexpr std::size_t kPatchCommitsPerCheck = 3000;

	// The most commits of a line tried as the place a branch's changes landed.
	inline constexpr std::size_t kLandingTries = 64;

	// The most line commits one job of a search for a commit's copy reads beyond what its earlier jobs read, so the
	// next check waits for one slice of a search and not the whole of it.
	inline constexpr std::size_t kPatchCommitsPerCopySlice = 100;
}
