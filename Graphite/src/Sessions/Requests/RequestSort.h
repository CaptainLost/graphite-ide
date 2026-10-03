#pragma once

#include "Sessions/Pricing.h"
#include "Sessions/Requests/RequestTimeline.h"

#include <cstddef>
#include <string>
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

	// One column's value for one row, as the table compares them. A row without the value —
	// the cell shows nothing — has Has false.
	struct SortValue
	{
		bool Has = false;
		double Number = 0.0;
		std::string Text;
	};

	// Whether `column` compares its values as text rather than as numbers.
	bool IsTextColumn(RequestColumn column);

	// Below zero when `left` goes ahead of `right` in a column sorted `ascending`, above zero when
	// it goes after, zero when neither does. A blank goes after any value, in either direction.
	int CompareSortValues(const SortValue& left, const SortValue& right, bool textual, bool ascending);

	// Sorts `order` — positions in `requests` — by `column`. A value the request does not
	// have (no usage, no delta, no wait, a cost with no price, no stop reason, no tools) sorts
	// last whichever way the column runs: flipping the direction must not bring every blank
	// to the top. Equal values keep the session's order, in either direction.
	void SortRequests(std::vector<std::size_t>& order, const std::vector<ApiRequest>& requests, RequestColumn column,
					  bool ascending, const Pricing& pricing);
}
