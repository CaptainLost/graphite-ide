#include "Git/History/HistoryStatus.h"

#include "Core/Base/Count.h"

#include <format>

namespace Graphite {

	namespace {

		constexpr const char* kLoading = "Loading…";
		constexpr const char* kSearching = "Searching…";
		constexpr const char* kCounting = " · counting…";

		const char* CommitsWord(std::size_t count)
		{
			return count == 1 ? "commit" : "commits";
		}

		std::string UnfilteredText(const HistoryStatusInput& input)
		{
			const std::string shown = Core::FormatCount(input.Shown);

			if (input.Complete)
			{
				return input.Shown == 1 ? std::string{"1 commit"} : std::format("All {} commits", shown);
			}

			if (input.Total.has_value())
			{
				return std::format("{} of {} {}", shown, Core::FormatCount(*input.Total), CommitsWord(*input.Total));
			}

			const std::string text = std::format("{} {}", shown, CommitsWord(input.Shown));

			return input.Counting ? text + kCounting : text;
		}

		std::string FilteredText(const HistoryStatusInput& input)
		{
			const std::string matches =
				std::format("{} matching {}", Core::FormatCount(input.Shown), CommitsWord(input.Shown));
			const std::string walked = Core::FormatCount(input.Walked);

			if (input.Complete)
			{
				return std::format("{} in all {}", matches, walked);
			}

			if (input.Total.has_value())
			{
				return std::format("{} in the newest {} of {}", matches, walked, Core::FormatCount(*input.Total));
			}

			return std::format("{} in the newest {}", matches, walked);
		}
	}

	std::optional<HistoryStatus> DescribeHistory(const HistoryStatusInput& input)
	{
		if (input.Stash || (!input.Filtered && input.Shown == 0))
		{
			return std::nullopt;
		}

		HistoryStatus status;
		status.Text = input.Filtered ? FilteredText(input) : UnfilteredText(input);

		if (input.Asking)
		{
			status.Busy = input.AskedFiltered ? kSearching : kLoading;
			return status;
		}

		if (!input.Complete)
		{
			status.Actions = input.Filtered ? HistoryActions::Search : HistoryActions::Load;
		}

		return status;
	}
}
