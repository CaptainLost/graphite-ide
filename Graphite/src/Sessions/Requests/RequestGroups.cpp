#include "Sessions/Requests/RequestGroups.h"

#include "Sessions/SessionSummary.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace Graphite {

	namespace {

		constexpr std::size_t kFirstTurn = 0;
		constexpr std::int64_t kNoDelta = 0;
		constexpr std::chrono::milliseconds kNoWait{0};
		constexpr std::string_view kMixedModelsLabel = "mixed";

		TurnSummary SummariseTurnRequests(std::size_t turnIndex, const std::vector<std::size_t>& positions,
										  const std::vector<ApiRequest>& requests, const Pricing& pricing)
		{
			TurnSummary summary;
			summary.Turn = turnIndex;
			summary.Count = positions.size();

			const ApiRequest* earliestRequest = nullptr;
			const ApiRequest* latestRequest = nullptr;
			const ApiRequest* latestRequestWithUsage = nullptr;
			UsageByModel usageByModel;

			for (const std::size_t position : positions)
			{
				const ApiRequest& request = requests[position];

				if (earliestRequest == nullptr || request.Index < earliestRequest->Index)
				{
					earliestRequest = &request;
				}

				if (latestRequest == nullptr || request.Index > latestRequest->Index)
				{
					latestRequest = &request;
				}

				if (!request.Model.empty())
				{
					if (summary.Model.empty())
					{
						summary.Model = request.Model;
						summary.Fast = request.Fast;
					}
					else if (request.Model != summary.Model || request.Fast != summary.Fast)
					{
						summary.MixedModels = true;
					}
				}

				if (request.ContextDelta)
				{
					summary.Delta = summary.Delta.value_or(kNoDelta) + *request.ContextDelta;
				}

				if (request.Waited)
				{
					summary.Waited = summary.Waited.value_or(kNoWait) + *request.Waited;
				}

				if (request.CacheRebuilt)
				{
					++summary.Rebuilt;
				}

				if (request.Usage.Total() == 0)
				{
					continue;
				}

				summary.HasUsage = true;
				summary.CacheRead += request.Usage.CacheRead;
				summary.ContextSum += request.Context;
				summary.Output += request.Usage.Output;
				usageByModel[UsageKey{request.Model, request.Fast}] += request.Usage;

				if (latestRequestWithUsage == nullptr || request.Index > latestRequestWithUsage->Index)
				{
					latestRequestWithUsage = &request;
				}
			}

			if (earliestRequest != nullptr)
			{
				summary.FirstIndex = earliestRequest->Index;
				summary.FirstRespondedAt = earliestRequest->RespondedAt;
			}

			if (latestRequest != nullptr)
			{
				summary.StopReason = latestRequest->StopReason;
			}

			if (latestRequestWithUsage != nullptr)
			{
				summary.Context = latestRequestWithUsage->Context;
			}

			summary.Cost = pricing.CostOf(usageByModel);
			return summary;
		}

		SortValue TurnSortValue(const TurnSummary& summary, RequestColumn column, const std::vector<std::string>& prompts)
		{
			switch (column)
			{
			case RequestColumn::Index:
				return NumericSortValue(static_cast<double>(summary.FirstIndex));
			case RequestColumn::Time:
			{
				const std::optional<std::chrono::sys_time<std::chrono::milliseconds>> respondedAt =
					ParseTimestamp(summary.FirstRespondedAt);
				return respondedAt ? NumericSortValue(static_cast<double>(respondedAt->time_since_epoch().count()))
								   : SortValue{};
			}
			case RequestColumn::Model:
				return TextualSortValue(summary.MixedModels ? std::string{kMixedModelsLabel} : summary.Model);
			case RequestColumn::Context:
				return summary.HasUsage ? NumericSortValue(static_cast<double>(summary.Context)) : SortValue{};
			case RequestColumn::Delta:
				return summary.Delta ? NumericSortValue(static_cast<double>(*summary.Delta)) : SortValue{};
			case RequestColumn::CachedShare:
				return summary.ContextSum > 0 ? NumericSortValue(static_cast<double>(summary.CacheRead) /
																 static_cast<double>(summary.ContextSum))
											  : SortValue{};
			case RequestColumn::Output:
				return summary.HasUsage ? NumericSortValue(static_cast<double>(summary.Output)) : SortValue{};
			case RequestColumn::Cost:
				return summary.HasUsage && summary.Cost.IsKnown() ? NumericSortValue(summary.Cost.Dollars)
																  : SortValue{};
			case RequestColumn::Wait:
				return summary.Waited ? NumericSortValue(static_cast<double>(summary.Waited->count())) : SortValue{};
			case RequestColumn::Stop:
				return TextualSortValue(summary.StopReason);
			case RequestColumn::Tools:
				return TextualSortValue(summary.Turn < prompts.size() ? prompts[summary.Turn] : std::string{});
			case RequestColumn::Rebuilt:
				return NumericSortValue(static_cast<double>(summary.Rebuilt));
			}

			return {};
		}

		struct GroupOrder
		{
			const std::vector<SortValue>& Values;
			const std::vector<TurnGroup>& Groups;
			bool Textual;
			bool Ascending;

			bool operator()(std::size_t leftGroup, std::size_t rightGroup) const
			{
				const int compared = CompareSortValues(Values[leftGroup], Values[rightGroup], Textual, Ascending);
				return compared != 0 ? compared < 0 : Groups[leftGroup].Summary.Turn < Groups[rightGroup].Summary.Turn;
			}
		};
	}

	std::vector<std::size_t> TurnOfRequests(const std::vector<ApiRequest>& requests, const std::vector<Turn>& turns)
	{
		std::unordered_map<std::string, std::size_t> turnByKey;

		for (std::size_t turnIndex = 0; turnIndex < turns.size(); ++turnIndex)
		{
			// WARNING: Every turn with a keyless response counts it under the empty key, so that key
			// names no request in particular.
			for (const auto& [key, usage] : turns[turnIndex].Requests)
			{
				if (!key.empty())
				{
					turnByKey.emplace(key, turnIndex);
				}
			}
		}

		std::vector<std::size_t> turnOf(requests.size(), kFirstTurn);

		for (std::size_t position = 0; position < requests.size(); ++position)
		{
			const std::unordered_map<std::string, std::size_t>::iterator found =
				requests[position].Key.empty() ? turnByKey.end() : turnByKey.find(requests[position].Key);

			if (found != turnByKey.end())
			{
				const auto& [matchedKey, matchedTurn] = *found;
				turnOf[position] = matchedTurn;
			}
			else if (position > 0)
			{
				turnOf[position] = turnOf[position - 1];
			}
		}

		return turnOf;
	}

	std::vector<TurnGroup> GroupByTurn(const std::vector<std::size_t>& order, const std::vector<std::size_t>& turnOf,
									   const std::vector<ApiRequest>& requests, const std::vector<std::string>& prompts,
									   RequestColumn column, bool ascending, const Pricing& pricing)
	{
		std::vector<TurnGroup> groups;
		std::unordered_map<std::size_t, std::size_t> groupOfTurn;

		for (const std::size_t position : order)
		{
			const std::size_t turnIndex = position < turnOf.size() ? turnOf[position] : kFirstTurn;
			const auto [found, added] = groupOfTurn.emplace(turnIndex, groups.size());

			if (added)
			{
				groups.emplace_back();
				groups.back().Summary.Turn = turnIndex;
			}

			const auto& [countedTurn, groupIndex] = *found;
			groups[groupIndex].Positions.push_back(position);
		}

		const bool columnIsSessionOrder = column == RequestColumn::Index || column == RequestColumn::Time;
		const bool reverseRequestsWithinTurn = !ascending && columnIsSessionOrder;

		for (TurnGroup& group : groups)
		{
			std::sort(group.Positions.begin(), group.Positions.end());

			if (reverseRequestsWithinTurn)
			{
				std::reverse(group.Positions.begin(), group.Positions.end());
			}

			group.Summary = SummariseTurnRequests(group.Summary.Turn, group.Positions, requests, pricing);
		}

		const bool textual = IsTextColumn(column);
		std::vector<SortValue> values;
		values.reserve(groups.size());

		for (const TurnGroup& group : groups)
		{
			values.push_back(TurnSortValue(group.Summary, column, prompts));
		}

		std::vector<std::size_t> sortedGroupIndices(groups.size());
		for (std::size_t groupIndex = 0; groupIndex < sortedGroupIndices.size(); ++groupIndex)
		{
			sortedGroupIndices[groupIndex] = groupIndex;
		}

		std::sort(sortedGroupIndices.begin(), sortedGroupIndices.end(), GroupOrder{values, groups, textual, ascending});

		std::vector<TurnGroup> sortedGroups;
		sortedGroups.reserve(groups.size());

		for (const std::size_t groupIndex : sortedGroupIndices)
		{
			sortedGroups.push_back(std::move(groups[groupIndex]));
		}

		return sortedGroups;
	}

}
