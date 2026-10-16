#include "Git/History/HistoryReach.h"

#include <algorithm>

namespace Graphite {

	namespace {

		// WARNING: A reach already at no limit stays there; adding a step to it would wrap around to
		// a page of a few hundred commits.
		std::size_t SaturatingAdd(std::size_t value, std::size_t step)
		{
			return value > kNoHistoryLimit - step ? kNoHistoryLimit : value + step;
		}
	}

	HistoryReach LoadMore(const HistoryReach& reach)
	{
		const std::size_t maxCount = SaturatingAdd(reach.MaxCount, kHistoryPageStep);

		return HistoryReach{.MaxCount = maxCount, .MaxWalk = std::max(reach.MaxWalk, maxCount)};
	}

	HistoryReach SearchFurther(std::size_t matchesShown, std::size_t walked)
	{
		return HistoryReach{.MaxCount = SaturatingAdd(matchesShown, kHistoryPageStep),
							.MaxWalk = SaturatingAdd(walked, kHistoryWalkStep)};
	}

	HistoryReach Everything()
	{
		return HistoryReach{.MaxCount = kNoHistoryLimit, .MaxWalk = kNoHistoryLimit};
	}

	HistoryReach ReachAfterClick(bool all, bool filtered, const HistoryReach& shown, std::size_t matchesShown,
								 std::size_t walked)
	{
		if (all)
		{
			return Everything();
		}

		if (filtered)
		{
			return SearchFurther(matchesShown, walked);
		}

		return LoadMore(shown);
	}
}
