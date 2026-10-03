#include "Sessions/Requests/RequestSort.h"

#include "Sessions/SessionSummary.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

namespace Graphite {

	namespace {

		// A column's value for one request is worked out once before sorting rather than in
		// every comparison: a cost or a request's tools take a moment to find.
		using SortKey = SortValue;

		SortKey Numeric(std::optional<double> value)
		{
			return value ? SortKey{.Has = true, .Number = *value} : SortKey{};
		}

		SortKey Textual(std::string text)
		{
			const bool has = !text.empty();
			return {.Has = has, .Text = std::move(text)};
		}

		SortKey KeyOf(const ApiRequest& request, RequestColumn column, const Pricing& pricing)
		{
			// The usage columns are blank, not zero, for a request that never had usage: the row
			// shows nothing there, and it sorts with the other blanks.
			const bool hasUsage = request.Usage.Total() > 0;

			switch (column)
			{
			case RequestColumn::Index:
				return Numeric(static_cast<double>(request.Index));
			case RequestColumn::Time:
			{
				const auto at = ParseTimestamp(request.RespondedAt);
				return at ? Numeric(static_cast<double>(at->time_since_epoch().count())) : SortKey{};
			}
			case RequestColumn::Model:
				return Textual(request.Model);
			case RequestColumn::Context:
				return hasUsage ? Numeric(static_cast<double>(request.Context)) : SortKey{};
			case RequestColumn::Delta:
				return request.ContextDelta ? Numeric(static_cast<double>(*request.ContextDelta)) : SortKey{};
			case RequestColumn::CachedShare:
				return hasUsage && request.Context > 0 ? Numeric(request.CachedShare) : SortKey{};
			case RequestColumn::Output:
				return hasUsage ? Numeric(static_cast<double>(request.Usage.Output)) : SortKey{};
			case RequestColumn::Cost:
			{
				if (!hasUsage)
				{
					return {};
				}

				const CostResult cost = pricing.CostOf(request);
				return cost.IsKnown() ? Numeric(cost.Dollars) : SortKey{};
			}
			case RequestColumn::Wait:
				return request.Waited ? Numeric(static_cast<double>(request.Waited->count())) : SortKey{};
			case RequestColumn::Stop:
				return Textual(request.StopReason);
			case RequestColumn::Tools:
			{
				std::string joined;
				for (const std::string& name : ToolNamesUsed(request))
				{
					joined += (joined.empty() ? "" : ", ") + name;
				}
				return Textual(std::move(joined));
			}
			case RequestColumn::Rebuilt:
				return Numeric(request.CacheRebuilt ? 1.0 : 0.0);
			}

			return {};
		}
	}

	bool IsTextColumn(RequestColumn column)
	{
		return column == RequestColumn::Model || column == RequestColumn::Stop || column == RequestColumn::Tools;
	}

	int CompareSortValues(const SortValue& left, const SortValue& right, bool textual, bool ascending)
	{
		if (left.Has != right.Has)
		{
			return left.Has ? -1 : 1;
		}

		if (!left.Has)
		{
			return 0;
		}

		const int compared = textual ? left.Text.compare(right.Text)
									 : (left.Number < right.Number ? -1 : (right.Number < left.Number ? 1 : 0));

		return ascending ? compared : -compared;
	}

	void SortRequests(std::vector<std::size_t>& order, const std::vector<ApiRequest>& requests, RequestColumn column,
					  bool ascending, const Pricing& pricing)
	{
		std::vector<SortKey> keys(requests.size());
		for (const std::size_t position : order)
		{
			keys[position] = KeyOf(requests[position], column, pricing);
		}

		const bool textual = IsTextColumn(column);

		// A total order on (blank last, value in the asked direction, position), so the result
		// does not depend on the order `order` came in.
		std::sort(order.begin(), order.end(),
				  [&keys, textual, ascending](std::size_t a, std::size_t b) {
					  const int compared = CompareSortValues(keys[a], keys[b], textual, ascending);
					  return compared != 0 ? compared < 0 : a < b;
				  });
	}
}
