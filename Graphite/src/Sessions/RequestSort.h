#pragma once

#include "Sessions/Pricing.h"
#include "Sessions/RequestTimeline.h"

#include <cstddef>
#include <vector>

namespace Graphite {

	// The columns the Requests tab can be sorted by. The values double as the table's column
	// ids, so a column keeps its sort however the reader reorders or hides the others.
	enum class RequestColumn
	{
		Index,
		Time,
		Model,
		Context,
		Delta,
		CachedShare,
		Output,
		Cost,
		Wait,
		Stop,
		Tools,
		Rebuilt
	};

	// Sorts `order` — positions in `requests` — by `column`. A value the request does not
	// have (no usage, no delta, no wait, a cost with no price, no stop reason, no tools) sorts
	// last whichever way the column runs: flipping the direction must not bring every blank
	// to the top. Equal values keep the session's order, in either direction.
	void SortRequests(std::vector<std::size_t>& order, const std::vector<ApiRequest>& requests, RequestColumn column,
					  bool ascending, const Pricing& pricing);
}
