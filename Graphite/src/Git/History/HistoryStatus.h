#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace Graphite {

	// Which pair of buttons the status line offers.
	enum class HistoryActions
	{
		None,
		// Load 500 more, Load all.
		Load,
		// Search further, Search all.
		Search
	};

	struct HistoryStatusInput
	{
		std::size_t Shown = 0;
		std::size_t Walked = 0;
		bool Complete = true;
		bool Filtered = false;
		bool Stash = false;
		std::optional<std::size_t> Total;
		bool Counting = false;

		// A query the reader started, with a click, a new ref or a new filter, has not answered yet.
		bool Asking = false;

		// Whether the query running narrows the history, which Filtered cannot say while the page on
		// screen still answers the query before it. WARNING: Last, since the callers' designated
		// initializers follow the order of the fields.
		bool AskedFiltered = false;
	};

	struct HistoryStatus
	{
		std::string Text;
		HistoryActions Actions = HistoryActions::None;

		// "Loading…" or "Searching…" while the reader's query runs, standing where the buttons would;
		// empty otherwise.
		std::string Busy;
	};

	// The line under the commit list, or none for a stash and for an empty history without a filter.
	std::optional<HistoryStatus> DescribeHistory(const HistoryStatusInput& input);
}
