#include "Sessions/RequestSort.h"

#include "Sessions/SessionSummary.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

namespace Graphite {

	namespace {

		// A column's value for one request, worked out once before sorting rather than in
		// every comparison: a cost or a request's tools take a moment to find.
		struct SortKey
		{
			bool Has = false;
			double Number = 0.0;
			std::string Text;
		};

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
			case RequestColumn::Index: return Numeric(static_cast<double>(request.Index));
			case RequestColumn::Time:
			{
				const auto at = ParseTimestamp(request.RespondedAt);
				return at ? Numeric(static_cast<double>(at->time_since_epoch().count())) : SortKey{};
			}
			case RequestColumn::Model: return Textual(request.Model);
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
					return {};

				const CostResult cost = pricing.CostOf({{UsageKey{request.Model, request.Fast}, request.Usage}});
				return cost.IsKnown() ? Numeric(cost.Dollars) : SortKey{};
			}
			case RequestColumn::Wait:
				return request.Waited ? Numeric(static_cast<double>(request.Waited->count())) : SortKey{};
			case RequestColumn::Stop: return Textual(request.StopReason);
			case RequestColumn::Tools:
			{
				std::string joined;
				for (const std::string& name : ToolNamesUsed(request))
					joined += (joined.empty() ? "" : ", ") + name;
				return Textual(std::move(joined));
			}
			case RequestColumn::Rebuilt: return Numeric(request.CacheRebuilt ? 1.0 : 0.0);
			}

			return {};
		}
	}

	void SortRequests(std::vector<std::size_t>& order, const std::vector<ApiRequest>& requests, RequestColumn column,
					  bool ascending, const Pricing& pricing)
	{
		std::vector<SortKey> keys(requests.size());
		for (const std::size_t position : order)
			keys[position] = KeyOf(requests[position], column, pricing);

		const bool textual = column == RequestColumn::Model || column == RequestColumn::Stop || column == RequestColumn::Tools;

		// A total order on (blank last, value in the asked direction, position), so the result
		// does not depend on the order `order` came in.
		std::sort(order.begin(), order.end(),
				  [&keys, textual, ascending](std::size_t a, std::size_t b)
				  {
					  const SortKey& left = keys[a];
					  const SortKey& right = keys[b];

					  if (left.Has != right.Has)
						  return left.Has;

					  if (left.Has)
					  {
						  const int compared = textual ? left.Text.compare(right.Text)
													   : (left.Number < right.Number ? -1 : (right.Number < left.Number ? 1 : 0));

						  if (compared != 0)
							  return ascending ? compared < 0 : compared > 0;
					  }

					  return a < b;
				  });
	}
}
