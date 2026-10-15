#include "Git/Branches/BranchAge.h"

#include <format>

namespace Graphite {

	namespace {

		constexpr long long kSecondsPerMinute = 60;
		constexpr long long kSecondsPerHour = 60 * kSecondsPerMinute;
		constexpr long long kSecondsPerDay = 24 * kSecondsPerHour;
		constexpr long long kDaysPerWeek = 7;
		constexpr long long kDaysPerMonth = 30;
		constexpr long long kDaysPerYear = 365;

		constexpr long long kDaysCountedInDays = 14;
		constexpr long long kDaysCountedInWeeks = 63;
	}

	std::string FormatBranchAge(std::chrono::sys_seconds tip, std::chrono::sys_seconds now)
	{
		const long long secondsOld = (now - tip).count();

		if (secondsOld < kSecondsPerMinute)
		{
			return "now";
		}

		if (secondsOld < kSecondsPerHour)
		{
			return std::format("{}m", secondsOld / kSecondsPerMinute);
		}

		if (secondsOld < kSecondsPerDay)
		{
			return std::format("{}h", secondsOld / kSecondsPerHour);
		}

		const long long daysOld = secondsOld / kSecondsPerDay;

		if (daysOld < kDaysCountedInDays)
		{
			return std::format("{}d", daysOld);
		}

		if (daysOld < kDaysCountedInWeeks)
		{
			return std::format("{}w", daysOld / kDaysPerWeek);
		}

		if (daysOld < kDaysPerYear)
		{
			return std::format("{}mo", daysOld / kDaysPerMonth);
		}

		return std::format("{}y", daysOld / kDaysPerYear);
	}
}
