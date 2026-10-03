#include <doctest/doctest.h>

#include "Sessions/Requests/RequestGroups.h"

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

using namespace Graphite;

namespace {

	// A request whose whole context was read from the cache, keyed "r<index>".
	ApiRequest Request(std::size_t index, std::uint64_t context, std::uint64_t output = 1)
	{
		ApiRequest request;
		request.Index = index;
		request.Key = "r" + std::to_string(index);
		request.Model = "claude-opus-5";
		request.RespondedAt = "2026-09-17T09:00:" + std::string(index < 10 ? "0" : "") + std::to_string(index) + ".000Z";
		request.Usage.CacheRead = context;
		request.Usage.Output = output;
		request.Context = context;
		request.CachedShare = context > 0 ? 1.0 : 0.0;
		request.StopReason = "tool_use";
		return request;
	}

	// A turn that counted the requests with these indices.
	Turn TurnOf(std::string prompt, const std::vector<std::size_t>& indices)
	{
		Turn turn;
		turn.Prompt = std::move(prompt);

		for (const std::size_t index : indices)
		{
			turn.Requests["r" + std::to_string(index)] = {};
		}

		return turn;
	}

	std::vector<std::size_t> All(const std::vector<ApiRequest>& requests)
	{
		std::vector<std::size_t> order;
		for (std::size_t position = 0; position < requests.size(); ++position)
		{
			order.push_back(position);
		}
		return order;
	}

	std::vector<std::size_t> TurnsOf(const std::vector<TurnGroup>& groups)
	{
		std::vector<std::size_t> turns;
		for (const TurnGroup& group : groups)
		{
			turns.push_back(group.Summary.Turn);
		}
		return turns;
	}

	std::vector<TurnGroup> Grouped(const std::vector<ApiRequest>& requests, const std::vector<Turn>& turns,
								   const std::vector<std::size_t>& order, RequestColumn column, bool ascending)
	{
		std::vector<std::string> prompts;
		for (const Turn& turn : turns)
		{
			prompts.push_back(turn.Prompt);
		}

		return GroupByTurn(order, TurnOfRequests(requests, turns), requests, prompts, column, ascending,
						   Pricing::Defaults());
	}
}

TEST_CASE("A request belongs to the turn that counted it")
{
	const std::vector<ApiRequest> requests{Request(1, 10), Request(2, 20), Request(3, 30)};
	const std::vector<Turn> turns{TurnOf("fix", {1, 2}), TurnOf("commit", {3})};

	CHECK(TurnOfRequests(requests, turns) == std::vector<std::size_t>{0, 0, 1});
}

TEST_CASE("A request no turn counted joins the request before it")
{
	// A request with no usage yet — the one still being written — is in no turn's Requests.
	const std::vector<ApiRequest> requests{Request(1, 10), Request(2, 20), Request(3, 30)};
	const std::vector<Turn> turns{TurnOf("fix", {1}), TurnOf("commit", {2})};

	CHECK(TurnOfRequests(requests, turns) == std::vector<std::size_t>{0, 1, 1});
}

TEST_CASE("A first request no turn counted belongs to the first turn")
{
	const std::vector<ApiRequest> requests{Request(1, 10), Request(2, 20)};

	CHECK(TurnOfRequests(requests, {TurnOf("fix", {2})}) == std::vector<std::size_t>{0, 0});
	CHECK(TurnOfRequests(requests, {}) == std::vector<std::size_t>{0, 0});
}

TEST_CASE("A request with no key joins the request before it, not whichever turn counted an empty key first")
{
	// A response with neither requestId nor uuid is counted under the empty key, in every turn
	// that has one: the empty key names no request in particular.
	ApiRequest keyless = Request(3, 30);
	keyless.Key.clear();

	const std::vector<ApiRequest> requests{Request(1, 10), Request(2, 20), keyless};

	Turn first = TurnOf("fix", {1});
	first.Requests[""] = {};

	CHECK(TurnOfRequests(requests, {first, TurnOf("commit", {2})}) == std::vector<std::size_t>{0, 1, 1});
}

TEST_CASE("Sorted by #, turns and their requests run in session order")
{
	const std::vector<ApiRequest> requests{Request(1, 10), Request(2, 20), Request(3, 30), Request(4, 40)};
	const std::vector<Turn> turns{TurnOf("fix", {1, 2}), TurnOf("commit", {3, 4})};

	const std::vector<TurnGroup> groups = Grouped(requests, turns, All(requests), RequestColumn::Index, true);

	REQUIRE(groups.size() == 2);
	CHECK(groups[0].Summary.Turn == 0);
	CHECK(groups[0].Positions == std::vector<std::size_t>{0, 1});
	CHECK(groups[1].Summary.Turn == 1);
	CHECK(groups[1].Positions == std::vector<std::size_t>{2, 3});

	const std::vector<TurnGroup> descending = Grouped(requests, turns, All(requests), RequestColumn::Index, false);
	CHECK(TurnsOf(descending) == std::vector<std::size_t>{1, 0});
	CHECK(descending[0].Positions == std::vector<std::size_t>{3, 2});
}

TEST_CASE("Turns move whole by their total, their requests staying in session order")
{
	// The turn of two small outputs costs less than the turn of one large one. Within the first
	// turn the second request's output is the smaller, and still it comes after the first.
	const std::vector<ApiRequest> requests{Request(1, 10, 300), Request(2, 10, 100), Request(3, 10, 5000)};
	const std::vector<Turn> turns{TurnOf("fix", {1, 2}), TurnOf("commit", {3})};

	const std::vector<TurnGroup> descending = Grouped(requests, turns, {2, 0, 1}, RequestColumn::Output, false);
	CHECK(TurnsOf(descending) == std::vector<std::size_t>{1, 0});
	CHECK(descending[1].Positions == std::vector<std::size_t>{0, 1});

	const std::vector<TurnGroup> ascending = Grouped(requests, turns, {1, 0, 2}, RequestColumn::Output, true);
	CHECK(TurnsOf(ascending) == std::vector<std::size_t>{0, 1});
	CHECK(ascending[0].Positions == std::vector<std::size_t>{0, 1});
}

TEST_CASE("Sorted back to front by time, a turn's requests run back to front too")
{
	const std::vector<ApiRequest> requests{Request(1, 10), Request(2, 20), Request(3, 30)};
	const std::vector<Turn> turns{TurnOf("fix", {1, 2}), TurnOf("commit", {3})};

	const std::vector<TurnGroup> groups = Grouped(requests, turns, All(requests), RequestColumn::Time, false);

	CHECK(TurnsOf(groups) == std::vector<std::size_t>{1, 0});
	CHECK(groups[1].Positions == std::vector<std::size_t>{1, 0});
}

TEST_CASE("A turn sorts by its prompt under Tools")
{
	const std::vector<ApiRequest> requests{Request(1, 10), Request(2, 20)};
	const std::vector<Turn> turns{TurnOf("zebra", {1}), TurnOf("apple", {2})};

	CHECK(TurnsOf(Grouped(requests, turns, All(requests), RequestColumn::Tools, true)) ==
		  std::vector<std::size_t>{1, 0});
}

TEST_CASE("A turn with no value for the column sorts last either way")
{
	ApiRequest pending = Request(3, 0, 0);
	pending.Usage = {};
	pending.Context = 0;

	const std::vector<ApiRequest> requests{Request(1, 10), Request(2, 20), pending};
	const std::vector<Turn> turns{TurnOf("a", {1}), TurnOf("b", {2}), TurnOf("c", {3})};

	CHECK(TurnsOf(Grouped(requests, turns, All(requests), RequestColumn::Cost, true)) ==
		  std::vector<std::size_t>{0, 1, 2});
	CHECK(TurnsOf(Grouped(requests, turns, All(requests), RequestColumn::Cost, false)) ==
		  std::vector<std::size_t>{1, 0, 2});
}

TEST_CASE("A turn's summary adds up the requests shown in it")
{
	ApiRequest first = Request(1, 100, 10);
	first.ContextDelta = 100;
	first.Waited = std::chrono::milliseconds(1000);
	first.CacheRebuilt = true;

	ApiRequest second = Request(2, 150, 20);
	second.ContextDelta = 50;
	second.Waited = std::chrono::milliseconds(2500);
	second.StopReason = "end_turn";

	const std::vector<ApiRequest> requests{first, second};
	const std::vector<TurnGroup> groups =
		Grouped(requests, {TurnOf("fix", {1, 2})}, {1, 0}, RequestColumn::Index, false);

	REQUIRE(groups.size() == 1);
	const TurnSummary& summary = groups[0].Summary;

	CHECK(summary.Count == 2);
	CHECK(summary.FirstIndex == 1);
	CHECK(summary.FirstRespondedAt == first.RespondedAt);
	CHECK(summary.Model == "claude-opus-5");
	CHECK_FALSE(summary.MixedModels);
	CHECK(summary.HasUsage);
	CHECK(summary.Context == 150);
	REQUIRE(summary.Delta.has_value());
	CHECK(*summary.Delta == 150);
	CHECK(summary.CacheRead == 250);
	CHECK(summary.ContextSum == 250);
	CHECK(summary.Output == 30);
	CHECK(summary.Cost.IsKnown());
	CHECK(summary.Cost.Dollars > 0.0);
	REQUIRE(summary.Waited.has_value());
	CHECK(summary.Waited->count() == 3500);
	CHECK(summary.StopReason == "end_turn");
	CHECK(summary.Rebuilt == 1);
}

TEST_CASE("A turn whose requests ran on different models says so")
{
	ApiRequest other = Request(2, 10);
	other.Model = "claude-haiku-4-5";

	const std::vector<ApiRequest> requests{Request(1, 10), other};
	const std::vector<TurnGroup> groups =
		Grouped(requests, {TurnOf("fix", {1, 2})}, All(requests), RequestColumn::Index, true);

	CHECK(groups[0].Summary.MixedModels);
}

TEST_CASE("A turn on mixed models sorts as \"mixed\", not with the blanks")
{
	ApiRequest other = Request(2, 10);
	other.Model = "claude-haiku-4-5";

	const std::vector<ApiRequest> requests{Request(1, 10), other, Request(3, 10)};
	const std::vector<Turn> turns{TurnOf("fix", {1, 2}), TurnOf("commit", {3})};

	// "claude-opus-5" sorts ahead of "mixed".
	CHECK(TurnsOf(Grouped(requests, turns, All(requests), RequestColumn::Model, true)) ==
		  std::vector<std::size_t>{1, 0});
	CHECK(TurnsOf(Grouped(requests, turns, All(requests), RequestColumn::Model, false)) ==
		  std::vector<std::size_t>{0, 1});
}

TEST_CASE("A turn the filters leave nothing of is not shown, and the rest count only what is")
{
	const std::vector<ApiRequest> requests{Request(1, 10, 100), Request(2, 20, 200), Request(3, 30, 300)};
	const std::vector<Turn> turns{TurnOf("fix", {1, 2}), TurnOf("commit", {3})};

	const std::vector<TurnGroup> groups = Grouped(requests, turns, {1}, RequestColumn::Index, true);

	REQUIRE(groups.size() == 1);
	CHECK(groups[0].Summary.Turn == 0);
	CHECK(groups[0].Summary.Count == 1);
	CHECK(groups[0].Summary.Output == 200);
	CHECK(groups[0].Summary.FirstIndex == 2);
}
