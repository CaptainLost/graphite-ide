#pragma once

#include <cstddef>

namespace Core {

	// The most commits of a line a patch-equivalence check walks for one branch.
	inline constexpr std::size_t kPatchCommitsPerCheck = 3000;

	// The most commits of a line tried as the place a branch's changes landed.
	inline constexpr std::size_t kLandingTries = 64;
}
