#pragma once

#include "Sessions/Pricing.h"
#include "Sessions/Requests/RequestTimeline.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

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

	struct SortValue
	{
		bool Has = false;
		double Number = 0.0;
		std::string Text;
	};

	SortValue NumericSortValue(std::optional<double> value);

	SortValue TextualSortValue(std::string text);

	bool IsTextColumn(RequestColumn column);

	int CompareSortValues(const SortValue& firstValue, const SortValue& secondValue, bool textual, bool ascending);

	void SortRequests(std::vector<std::size_t>& order, const std::vector<ApiRequest>& requests, RequestColumn column,
					  bool ascending, const Pricing& pricing);
}
