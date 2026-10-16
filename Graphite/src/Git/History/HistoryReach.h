#pragma once

#include "Core/Git/Requests.h"

#include <cstddef>
#include <limits>

namespace Graphite {

	inline constexpr std::size_t kHistoryPageStep = Core::kLogPageSize;
	inline constexpr std::size_t kHistoryWalkStep = Core::kLogWalkLimit;
	inline constexpr std::size_t kNoHistoryLimit = std::numeric_limits<std::size_t>::max();

	// How far the history asks its walk to go: the commits to keep and the commits to visit. Only
	// the reader's clicks extend it; a new ref, stash or filter starts it over.
	struct HistoryReach
	{
		std::size_t MaxCount = Core::kLogPageSize;
		std::size_t MaxWalk = Core::kLogWalkLimit;

		friend bool operator==(const HistoryReach&, const HistoryReach&) = default;
	};

	// 500 commits further. The ceiling never stays below the page, or a history could not be read
	// past its newest 20,000 commits without a filter.
	HistoryReach LoadMore(const HistoryReach& reach);

	// 500 matches further, or 20,000 commits deeper than the page on screen walked, whichever the
	// walk reaches first.
	HistoryReach SearchFurther(std::size_t matchesShown, std::size_t walked);

	// No limit on either, so the walk goes to the root.
	HistoryReach Everything();

	// The reach a click on the status line asks for: everything for the All buttons; otherwise 500
	// matches or 20,000 commits further than the page on screen with a filter, or 500 commits
	// further than the page's own reach without one.
	HistoryReach ReachAfterClick(bool all, bool filtered, const HistoryReach& shown, std::size_t matchesShown,
								 std::size_t walked);
}
