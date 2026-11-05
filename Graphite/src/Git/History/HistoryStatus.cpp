#include "Git/History/HistoryStatus.h"

#include "Core/Base/Count.h"

#include <string>

namespace Graphite {

	namespace {

		constexpr const char* kLoading = "Loading…";
		constexpr const char* kSearching = "Searching…";
		constexpr const char* kFiltered = "Filtered: the graph is hidden while a filter is on";
		constexpr const char* kRowsMeaning = " · first parents only: each merge, squash or direct commit is one row";
		constexpr const char* kCommitsMeaning = " · every commit with its graph";

		std::string UnfilteredText(const HistoryStatusInput& input)
		{
			const std::string shown = Core::FormatCount(input.Shown);

			if (input.FirstParentsOnly)
			{
				return shown + (input.Shown == 1 ? " row" : " rows") + kRowsMeaning;
			}

			return shown + (input.Shown == 1 ? " commit" : " commits") + kCommitsMeaning;
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
		status.Text = input.Filtered ? std::string{kFiltered} : UnfilteredText(input);

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
