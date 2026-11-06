#pragma once

#include <cstddef>
#include <cstdint>

namespace Core {

	inline constexpr std::int64_t kSecondsPerDay = 86400;

	inline constexpr std::size_t kMergedLinesForBase = 3;
	inline constexpr std::size_t kMergedAuthorsForBase = 2; // or the merges span kSpanForBase
	inline constexpr std::size_t kAuthorsForBase = 3;
	inline constexpr std::int64_t kSpanForBase = 7 * kSecondsPerDay;
	inline constexpr std::size_t kCommitsForBase = 5;
	inline constexpr std::int64_t kActiveWindow = 90 * kSecondsPerDay;

	// At a branch's tip the line stops when the line below has at least kAuthorsBelowAStack authors and the
	// work above has fewer than kAuthorsAboveAStack: that work is a stack built on the line, not the line going on.
	inline constexpr std::size_t kAuthorsBelowAStack = 3;
	inline constexpr std::size_t kAuthorsAboveAStack = 2;
}
