#include "Sessions/Requests/RequestSort.h"

#include "Core/Base/Strings.h"

#include "Sessions/SessionSummary.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <utility>

namespace Graphite {

	namespace {

		SortValue SortValueOf(const ApiRequest& request, RequestColumn column, const Pricing& pricing)
		{
			const bool hasUsage = request.Usage.Total() > 0;

			switch (column)
			{
			case RequestColumn::Index:
				return NumericSortValue(static_cast<double>(request.Index));
			case RequestColumn::Time:
			{
				const std::optional<std::chrono::sys_time<std::chrono::milliseconds>> respondedAt =
					ParseTimestamp(request.RespondedAt);
				return respondedAt ? NumericSortValue(static_cast<double>(respondedAt->time_since_epoch().count()))
								   : SortValue{};
			}
			case RequestColumn::Model:
				return TextualSortValue(request.Model);
			case RequestColumn::Context:
				return hasUsage ? NumericSortValue(static_cast<double>(request.Context)) : SortValue{};
			case RequestColumn::Delta:
				return request.ContextDelta ? NumericSortValue(static_cast<double>(*request.ContextDelta)) : SortValue{};
			case RequestColumn::CachedShare:
				return hasUsage && request.Context > 0 ? NumericSortValue(request.CachedShare) : SortValue{};
			case RequestColumn::Output:
				return hasUsage ? NumericSortValue(static_cast<double>(request.Usage.Output)) : SortValue{};
			case RequestColumn::Cost:
			{
				if (!hasUsage)
				{
					return {};
				}

				const CostResult cost = pricing.CostOf(request);
				return cost.IsKnown() ? NumericSortValue(cost.Dollars) : SortValue{};
			}
			case RequestColumn::Wait:
				return request.Waited ? NumericSortValue(static_cast<double>(request.Waited->count())) : SortValue{};
			case RequestColumn::Stop:
				return TextualSortValue(request.StopReason);
			case RequestColumn::Tools:
			{
				std::string joined;
				for (const std::string& name : ToolNamesUsed(request))
				{
					Core::AppendSeparated(joined, ", ", name);
				}
				return TextualSortValue(std::move(joined));
			}
			case RequestColumn::Rebuilt:
				return NumericSortValue(request.CacheRebuilt ? 1.0 : 0.0);
			}

			return {};
		}

		int CompareNumbers(double firstNumber, double secondNumber)
		{
			if (firstNumber < secondNumber)
			{
				return -1;
			}

			if (secondNumber < firstNumber)
			{
				return 1;
			}

			return 0;
		}

		bool IsRequestBefore(const std::vector<SortValue>& sortValues, bool textual, bool ascending, std::size_t firstPosition,
							 std::size_t secondPosition)
		{
			const int compared = CompareSortValues(sortValues[firstPosition], sortValues[secondPosition], textual, ascending);
			return compared != 0 ? compared < 0 : firstPosition < secondPosition;
		}
	}

	SortValue NumericSortValue(std::optional<double> value)
	{
		if (!value)
		{
			return SortValue{};
		}

		return SortValue{.Has = true, .Number = *value};
	}

	SortValue TextualSortValue(std::string text)
	{
		const bool hasText = !text.empty();

		return SortValue{.Has = hasText, .Text = std::move(text)};
	}

	bool IsTextColumn(RequestColumn column)
	{
		return column == RequestColumn::Model || column == RequestColumn::Stop || column == RequestColumn::Tools;
	}

	int CompareSortValues(const SortValue& firstValue, const SortValue& secondValue, bool textual, bool ascending)
	{
		if (firstValue.Has != secondValue.Has)
		{
			return firstValue.Has ? -1 : 1;
		}

		if (!firstValue.Has)
		{
			return 0;
		}

		const int comparison = textual ? firstValue.Text.compare(secondValue.Text)
									   : CompareNumbers(firstValue.Number, secondValue.Number);

		return ascending ? comparison : -comparison;
	}

	void SortRequests(std::vector<std::size_t>& order, const std::vector<ApiRequest>& requests, RequestColumn column,
					  bool ascending, const Pricing& pricing)
	{
		std::vector<SortValue> sortValues(requests.size());
		for (const std::size_t position : order)
		{
			sortValues[position] = SortValueOf(requests[position], column, pricing);
		}

		const bool textual = IsTextColumn(column);

		std::sort(order.begin(), order.end(),
				  [&sortValues, textual, ascending](std::size_t firstPosition, std::size_t secondPosition) { return IsRequestBefore(sortValues, textual, ascending, firstPosition, secondPosition); });
	}
}
