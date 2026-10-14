#include <doctest/doctest.h>

#include "Sessions/Requests/RequestSort.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <vector>

using namespace Graphite;

namespace {

	constexpr bool kAscending = true;
	constexpr bool kDescending = false;
	constexpr bool kNumeric = false;

	ApiRequest Request(std::size_t index, std::uint64_t context, std::uint64_t output = 1)
	{
		ApiRequest request;
		request.Index = index;
		request.Model = "claude-opus-5";
		request.RespondedAt = "2026-09-17T09:00:0" + std::to_string(index) + ".000Z";
		request.Usage.CacheRead = context;
		request.Usage.Output = output;
		request.Context = context;
		request.CachedShare = context > 0 ? 1.0 : 0.0;
		return request;
	}

	std::vector<std::size_t> Sorted(const std::vector<ApiRequest>& requests, RequestColumn column, bool ascending)
	{
		std::vector<std::size_t> order;
		for (std::size_t position = 0; position < requests.size(); ++position)
		{
			order.push_back(position);
		}

		SortRequests(order, requests, column, ascending, Pricing::Defaults());
		return order;
	}

	void CheckBothWays(const std::vector<ApiRequest>& requests, RequestColumn column)
	{
		CHECK(Sorted(requests, column, kAscending) == std::vector<std::size_t>{1, 0, 2});
		CHECK(Sorted(requests, column, kDescending) == std::vector<std::size_t>{2, 0, 1});
	}
}

TEST_CASE("Every numeric column sorts both ways")
{
	SUBCASE("Index")
	{
		std::vector<ApiRequest> requests{Request(2, 1), Request(1, 1), Request(3, 1)};
		CheckBothWays(requests, RequestColumn::Index);
	}

	SUBCASE("Time")
	{
		std::vector<ApiRequest> requests{Request(2, 1), Request(1, 1), Request(3, 1)};
		CheckBothWays(requests, RequestColumn::Time);
	}

	SUBCASE("Context")
	{
		std::vector<ApiRequest> requests{Request(1, 500), Request(2, 100), Request(3, 900)};
		CheckBothWays(requests, RequestColumn::Context);
	}

	SUBCASE("Delta")
	{
		std::vector<ApiRequest> requests{Request(1, 1), Request(2, 1), Request(3, 1)};
		requests[0].ContextDelta = 10;
		requests[1].ContextDelta = -40;
		requests[2].ContextDelta = 300;
		CheckBothWays(requests, RequestColumn::Delta);
	}

	SUBCASE("Cached share")
	{
		std::vector<ApiRequest> requests{Request(1, 10), Request(2, 10), Request(3, 10)};
		requests[0].CachedShare = 0.5;
		requests[1].CachedShare = 0.1;
		requests[2].CachedShare = 0.99;
		CheckBothWays(requests, RequestColumn::CachedShare);
	}

	SUBCASE("Output")
	{
		std::vector<ApiRequest> requests{Request(1, 10, 50), Request(2, 10, 5), Request(3, 10, 500)};
		CheckBothWays(requests, RequestColumn::Output);
	}

	SUBCASE("Cost")
	{
		std::vector<ApiRequest> requests{Request(1, 5'000), Request(2, 1'000), Request(3, 90'000)};
		CheckBothWays(requests, RequestColumn::Cost);
	}

	SUBCASE("Wait")
	{
		std::vector<ApiRequest> requests{Request(1, 1), Request(2, 1), Request(3, 1)};
		requests[0].Waited = std::chrono::milliseconds{2000};
		requests[1].Waited = std::chrono::milliseconds{300};
		requests[2].Waited = std::chrono::milliseconds{9000};
		CheckBothWays(requests, RequestColumn::Wait);
	}
}

TEST_CASE("A cost that is not known sorts last whichever way the column runs")
{
	std::vector<ApiRequest> requests{Request(1, 5'000), Request(2, 1'000), Request(3, 90'000)};
	requests[0].Model = "some-model-with-no-price";

	CHECK(Sorted(requests, RequestColumn::Cost, kAscending) == std::vector<std::size_t>{1, 2, 0});
	CHECK(Sorted(requests, RequestColumn::Cost, kDescending) == std::vector<std::size_t>{2, 1, 0});
}

TEST_CASE("A missing delta or wait sorts last whichever way the column runs")
{
	std::vector<ApiRequest> requests{Request(1, 1), Request(2, 1), Request(3, 1)};
	requests[1].ContextDelta = 5;
	requests[2].ContextDelta = 50;
	requests[0].Waited = std::chrono::milliseconds{100};
	requests[2].Waited = std::chrono::milliseconds{10};

	CHECK(Sorted(requests, RequestColumn::Delta, kAscending) == std::vector<std::size_t>{1, 2, 0});
	CHECK(Sorted(requests, RequestColumn::Delta, kDescending) == std::vector<std::size_t>{2, 1, 0});
	CHECK(Sorted(requests, RequestColumn::Wait, kAscending) == std::vector<std::size_t>{2, 0, 1});
	CHECK(Sorted(requests, RequestColumn::Wait, kDescending) == std::vector<std::size_t>{0, 2, 1});
}

TEST_CASE("Equal values keep the session's order in either direction")
{
	std::vector<ApiRequest> requests{Request(1, 100), Request(2, 700), Request(3, 100), Request(4, 100)};

	CHECK(Sorted(requests, RequestColumn::Context, kAscending) == std::vector<std::size_t>{0, 2, 3, 1});
	CHECK(Sorted(requests, RequestColumn::Context, kDescending) == std::vector<std::size_t>{1, 0, 2, 3});
}

TEST_CASE("A rebuilt cache sorts last ascending and first descending")
{
	std::vector<ApiRequest> requests{Request(1, 1), Request(2, 1), Request(3, 1)};
	requests[1].CacheRebuilt = true;

	CHECK(Sorted(requests, RequestColumn::Rebuilt, kAscending) == std::vector<std::size_t>{0, 2, 1});
	CHECK(Sorted(requests, RequestColumn::Rebuilt, kDescending) == std::vector<std::size_t>{1, 0, 2});
}

TEST_CASE("Text columns sort by their text, with an empty one last")
{
	std::vector<ApiRequest> requests{Request(1, 1), Request(2, 1), Request(3, 1)};
	requests[0].StopReason = "tool_use";
	requests[1].StopReason = "end_turn";

	requests[0].Model = "claude-sonnet-5";
	requests[2].Model = "claude-haiku-5";

	requests[1].Response.push_back({.Kind = ResponseKind::ToolCall, .Label = "Read a", .ToolName = "Read"});
	requests[2].Response.push_back({.Kind = ResponseKind::ToolCall, .Label = "Bash ls", .ToolName = "Bash"});

	CHECK(Sorted(requests, RequestColumn::Stop, kAscending) == std::vector<std::size_t>{1, 0, 2});
	CHECK(Sorted(requests, RequestColumn::Stop, kDescending) == std::vector<std::size_t>{0, 1, 2});
	CHECK(Sorted(requests, RequestColumn::Model, kAscending) == std::vector<std::size_t>{2, 1, 0});
	CHECK(Sorted(requests, RequestColumn::Tools, kAscending) == std::vector<std::size_t>{2, 1, 0});
	CHECK(Sorted(requests, RequestColumn::Tools, kDescending) == std::vector<std::size_t>{1, 2, 0});
}

TEST_CASE("A request with no usage sorts last on every usage column")
{
	std::vector<ApiRequest> requests{Request(1, 0, 0), Request(2, 300), Request(3, 100)};

	for (const RequestColumn column : {RequestColumn::Context, RequestColumn::CachedShare, RequestColumn::Output, RequestColumn::Cost})
	{
		CHECK(Sorted(requests, column, kAscending).back() == 0);
		CHECK(Sorted(requests, column, kDescending).back() == 0);
	}
}

TEST_CASE("A fast request sorts as dearer than the same usage at standard speed")
{
	std::vector<ApiRequest> requests{Request(1, 1'000), Request(2, 1'000), Request(3, 1'000)};
	requests[1].Fast = true;

	CHECK(Sorted(requests, RequestColumn::Cost, kAscending) == std::vector<std::size_t>{0, 2, 1});
}

TEST_CASE("A sort value is blank for a missing number or an empty text")
{
	const SortValue missing = NumericSortValue(std::nullopt);
	CHECK_FALSE(missing.Has);
	CHECK(missing.Number == 0.0);

	CHECK(NumericSortValue(2.5).Has);
	CHECK(NumericSortValue(2.5).Number == 2.5);
	CHECK(std::isnan(NumericSortValue(std::numeric_limits<double>::quiet_NaN()).Number));

	CHECK_FALSE(TextualSortValue("").Has);
	CHECK(TextualSortValue("x").Has);
	CHECK(TextualSortValue("x").Text == "x");
}

TEST_CASE("A blank sort value goes after any value in either direction")
{
	const SortValue blank = NumericSortValue(std::nullopt);
	const SortValue value = NumericSortValue(1.0);

	for (const bool ascending : {true, false})
	{
		CHECK(CompareSortValues(value, blank, kNumeric, ascending) < 0);
		CHECK(CompareSortValues(blank, value, kNumeric, ascending) > 0);
		CHECK(CompareSortValues(blank, blank, kNumeric, ascending) == 0);
	}
}
