#include "Git/History/HistoryStatus.h"

#include "Core/Base/Count.h"

#include <string>

namespace Graphite {

	namespace {

		constexpr const char* kLoading = "Loading…";
		constexpr const char* kSearching = "Searching…";
		constexpr const char* kCounting = "…";
		constexpr const char* kFiltered = "Filtered: the graph is hidden while a filter is on";
		constexpr const char* kRowsMeaning = "First parents only: each merge, squash or direct commit is one row";
		constexpr const char* kCommitsMeaning = "Every commit with its graph";

		std::string CountText(const HistoryStatusInput& input)
		{
			// WARNING: "of m" stands only while more can be loaded; its place says the total is being counted until it lands, and is left out when the count failed.
			const bool saysTotal = !input.Complete && (input.Total.has_value() || input.Counting);
			const std::size_t counted = saysTotal && input.Total.has_value() ? *input.Total : input.Shown;
			const std::string shown = Core::FormatCount(input.Shown) + (saysTotal ? " of " + (input.Total.has_value() ? Core::FormatCount(*input.Total) : std::string{kCounting}) : std::string{});
			const std::string noun = input.FirstParentsOnly ? (counted == 1 ? "row" : "rows") : (counted == 1 ? "commit" : "commits");

			return shown + " " + noun;
		}
	}

	std::optional<HistoryStatus> DescribeHistory(const HistoryStatusInput& input)
	{
		const char* busy = input.AskedFiltered ? kSearching : kLoading;
		const bool pageHasNothingToCount = input.Stash || (!input.Filtered && input.Shown == 0);

		if (pageHasNothingToCount)
		{
			if (!input.Asking)
			{
				return std::nullopt;
			}

			return HistoryStatus{.Busy = busy};
		}

		HistoryStatus status;

		if (input.Filtered)
		{
			status.Text = kFiltered;
		}
		else
		{
			status.Text = input.FirstParentsOnly ? kRowsMeaning : kCommitsMeaning;
			status.Count = CountText(input);
		}

		if (input.Asking)
		{
			status.Busy = busy;
			return status;
		}

		if (!input.Complete)
		{
			status.Actions = input.Filtered ? HistoryActions::Search : HistoryActions::Load;
		}

		return status;
	}
}
