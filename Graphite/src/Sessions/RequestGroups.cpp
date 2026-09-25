#include "Sessions/RequestGroups.h"

#include "Sessions/SessionSummary.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace Graphite {

	namespace {

		TurnSummary Summarise(std::size_t turn, const std::vector<std::size_t>& positions,
							  const std::vector<ApiRequest>& requests, const Pricing& pricing)
		{
			TurnSummary summary;
			summary.Turn = turn;
			summary.Count = positions.size();

			// The earliest and the latest request by Index, whatever order `positions` is in.
			const ApiRequest* first = nullptr;
			const ApiRequest* last = nullptr;
			const ApiRequest* lastWithUsage = nullptr;
			UsageByModel usage;

			for (const std::size_t position : positions)
			{
				const ApiRequest& request = requests[position];

				if (first == nullptr || request.Index < first->Index)
					first = &request;

				if (last == nullptr || request.Index > last->Index)
					last = &request;

				if (!request.Model.empty())
				{
					if (summary.Model.empty())
					{
						summary.Model = request.Model;
						summary.Fast = request.Fast;
					}
					else if (request.Model != summary.Model || request.Fast != summary.Fast)
						summary.MixedModels = true;
				}

				if (request.ContextDelta)
					summary.Delta = summary.Delta.value_or(0) + *request.ContextDelta;

				if (request.Waited)
					summary.Waited = summary.Waited.value_or(std::chrono::milliseconds{0}) + *request.Waited;

				if (request.CacheRebuilt)
					++summary.Rebuilt;

				if (request.Usage.Total() == 0)
					continue;

				summary.HasUsage = true;
				summary.CacheRead += request.Usage.CacheRead;
				summary.ContextSum += request.Context;
				summary.Output += request.Usage.Output;
				usage[UsageKey{request.Model, request.Fast}] += request.Usage;

				if (lastWithUsage == nullptr || request.Index > lastWithUsage->Index)
					lastWithUsage = &request;
			}

			if (first != nullptr)
			{
				summary.FirstIndex = first->Index;
				summary.FirstRespondedAt = first->RespondedAt;
			}

			if (last != nullptr)
				summary.StopReason = last->StopReason;

			if (lastWithUsage != nullptr)
				summary.Context = lastWithUsage->Context;

			summary.Cost = pricing.CostOf(usage);
			return summary;
		}

		SortValue Numeric(std::optional<double> value)
		{
			return value ? SortValue{.Has = true, .Number = *value} : SortValue{};
		}

		SortValue Textual(std::string text)
		{
			const bool has = !text.empty();
			return {.Has = has, .Text = std::move(text)};
		}

		// The same columns SortRequests reads, for a turn as a whole.
		SortValue ValueOf(const TurnSummary& summary, RequestColumn column, const std::vector<std::string>& prompts)
		{
			switch (column)
			{
			case RequestColumn::Index: return Numeric(static_cast<double>(summary.FirstIndex));
			case RequestColumn::Time:
			{
				const auto at = ParseTimestamp(summary.FirstRespondedAt);
				return at ? Numeric(static_cast<double>(at->time_since_epoch().count())) : SortValue{};
			}
			case RequestColumn::Model: return Textual(summary.MixedModels ? std::string{"mixed"} : summary.Model);
			case RequestColumn::Context:
				return summary.HasUsage ? Numeric(static_cast<double>(summary.Context)) : SortValue{};
			case RequestColumn::Delta:
				return summary.Delta ? Numeric(static_cast<double>(*summary.Delta)) : SortValue{};
			case RequestColumn::CachedShare:
				return summary.ContextSum > 0
						   ? Numeric(static_cast<double>(summary.CacheRead) / static_cast<double>(summary.ContextSum))
						   : SortValue{};
			case RequestColumn::Output:
				return summary.HasUsage ? Numeric(static_cast<double>(summary.Output)) : SortValue{};
			case RequestColumn::Cost:
				return summary.HasUsage && summary.Cost.IsKnown() ? Numeric(summary.Cost.Dollars) : SortValue{};
			case RequestColumn::Wait:
				return summary.Waited ? Numeric(static_cast<double>(summary.Waited->count())) : SortValue{};
			case RequestColumn::Stop: return Textual(summary.StopReason);
			case RequestColumn::Tools:
				return Textual(summary.Turn < prompts.size() ? prompts[summary.Turn] : std::string{});
			case RequestColumn::Rebuilt: return Numeric(static_cast<double>(summary.Rebuilt));
			}

			return {};
		}
	}

	std::vector<std::size_t> TurnOfRequests(const std::vector<ApiRequest>& requests, const std::vector<Turn>& turns)
	{
		std::unordered_map<std::string, std::size_t> byKey;

		for (std::size_t turn = 0; turn < turns.size(); ++turn)
		{
			// Every turn with a keyless response counts it under the empty key, so that key
			// names no request in particular.
			for (const auto& [key, usage] : turns[turn].Requests)
			{
				if (!key.empty())
					byKey.emplace(key, turn);
			}
		}

		std::vector<std::size_t> turnOf(requests.size(), 0);

		for (std::size_t position = 0; position < requests.size(); ++position)
		{
			const auto found = requests[position].Key.empty() ? byKey.end() : byKey.find(requests[position].Key);

			if (found != byKey.end())
				turnOf[position] = found->second;
			else if (position > 0)
				turnOf[position] = turnOf[position - 1];
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
			const std::size_t turn = position < turnOf.size() ? turnOf[position] : 0;
			const auto [found, added] = groupOfTurn.emplace(turn, groups.size());

			if (added)
			{
				groups.emplace_back();
				groups.back().Summary.Turn = turn;
			}

			groups[found->second].Positions.push_back(position);
		}

		// A turn's requests in session order, whatever order `order` gave them in: the turn moves
		// as a whole, and its requests read as they happened. Only a column that is itself the
		// session's order, run back to front, turns them round too, so the whole table does.
		const bool backwards = !ascending && (column == RequestColumn::Index || column == RequestColumn::Time);

		for (TurnGroup& group : groups)
		{
			std::sort(group.Positions.begin(), group.Positions.end());

			if (backwards)
				std::reverse(group.Positions.begin(), group.Positions.end());

			group.Summary = Summarise(group.Summary.Turn, group.Positions, requests, pricing);
		}

		const bool textual = IsTextColumn(column);
		std::vector<SortValue> values;
		values.reserve(groups.size());

		for (const TurnGroup& group : groups)
			values.push_back(ValueOf(group.Summary, column, prompts));

		std::vector<std::size_t> sorted(groups.size());
		for (std::size_t index = 0; index < sorted.size(); ++index)
			sorted[index] = index;

		// Ties go by turn, so equal turns keep the session's order in either direction.
		std::sort(sorted.begin(), sorted.end(),
				  [&values, &groups, textual, ascending](std::size_t a, std::size_t b)
				  {
					  const int compared = CompareSortValues(values[a], values[b], textual, ascending);
					  return compared != 0 ? compared < 0 : groups[a].Summary.Turn < groups[b].Summary.Turn;
				  });

		std::vector<TurnGroup> result;
		result.reserve(groups.size());

		for (const std::size_t index : sorted)
			result.push_back(std::move(groups[index]));

		return result;
	}

}
