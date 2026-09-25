#pragma once

#include "Sessions/Pricing.h"
#include "Sessions/RequestSort.h"
#include "Sessions/RequestTimeline.h"
#include "Sessions/Session.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace Graphite {

	// By position in `requests`, the index in `turns` of the turn each request belongs to: the
	// turn that counted its usage. A request no turn counted — one whose response has no usage
	// yet, or one with no key, which says nothing about where it was counted — belongs with the
	// request before it, and a first one with the first turn.
	std::vector<std::size_t> TurnOfRequests(const std::vector<ApiRequest>& requests, const std::vector<Turn>& turns);

	// What a turn's row in the Requests table shows, worked out from the requests of it the
	// filters let through.
	struct TurnSummary
	{
		std::size_t Turn = 0; // index in Session::GetTurns()
		std::size_t Count = 0;

		// The earliest request's Index and RespondedAt — not Turn::StartedAt, the prompt's time:
		// the row reads like the requests under it.
		std::size_t FirstIndex = 0;
		std::string FirstRespondedAt;

		// The model every request ran on, with Fast; MixedModels when they differ.
		std::string Model;
		bool Fast = false;
		bool MixedModels = false;

		// Whether any request had usage: without it the usage columns are blank.
		bool HasUsage = false;

		// The latest request's Context: how large the context had grown by the turn's end.
		std::uint64_t Context = 0;

		// The sum of the requests' deltas, nothing when none had one.
		std::optional<std::int64_t> Delta;

		// The cache reads and the contexts summed, for the share read from the cache.
		std::uint64_t CacheRead = 0;
		std::uint64_t ContextSum = 0;

		std::uint64_t Output = 0;
		CostResult Cost;

		// The waits summed, nothing when none had one.
		std::optional<std::chrono::milliseconds> Waited;

		// The latest request's: how the turn ended, or empty while it is still being answered.
		std::string StopReason;

		// How many of the requests rebuilt the cache.
		std::size_t Rebuilt = 0;
	};

	struct TurnGroup
	{
		TurnSummary Summary;

		// The turn's requests, as positions in `requests`, in the order they were given.
		std::vector<std::size_t> Positions;
	};

	// Groups `order` — positions in `requests`, already sorted and filtered for the table — by
	// the turn each belongs to, keeping their order within a turn, and sorts the turns by their
	// summary's value for `column` the way SortRequests sorts requests: blanks last, ties in
	// session order. Under Tools a turn's value is its prompt, from `prompts` by turn index,
	// since that is what its row shows there. A turn none of `order` belongs to is left out.
	std::vector<TurnGroup> GroupByTurn(const std::vector<std::size_t>& order, const std::vector<std::size_t>& turnOf,
									   const std::vector<ApiRequest>& requests, const std::vector<std::string>& prompts,
									   RequestColumn column, bool ascending, const Pricing& pricing);

	// One row of the grouped table: a turn's, naming its index in the groups, or a request's,
	// naming its position in `requests`.
	struct GroupedRow
	{
		bool IsTurn = false;
		std::size_t Group = 0;
		std::size_t Position = 0;
	};

	// The table's rows: each turn, followed by its requests when its turn index is in `expanded`.
	std::vector<GroupedRow> FlattenGroups(const std::vector<TurnGroup>& groups, const std::set<std::size_t>& expanded);
}
