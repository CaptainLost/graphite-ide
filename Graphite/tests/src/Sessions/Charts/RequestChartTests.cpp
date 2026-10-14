#include <doctest/doctest.h>

#include "Sessions/Charts/RequestChart.h"

#include "Sessions/Pricing.h"

#include <chrono>
#include <cmath>
#include <vector>

using namespace Graphite;

namespace {
	constexpr ContextLayers kContextLayerValues = {800.0, 30.0, 5.0};

	Pricing OnePrice()
	{
		Pricing pricing;
		pricing.Set("m", Price{3.0, 15.0, 3.75, 6.0, 0.3});
		return pricing;
	}

	ApiRequest At(const char* timestamp, std::optional<std::chrono::milliseconds> waited = std::nullopt)
	{
		ApiRequest request;
		request.RespondedAt = timestamp;
		request.Waited = waited;
		return request;
	}

	ApiRequest Span(const char* sentAt, const char* finishedAt)
	{
		ApiRequest request;
		request.SentAt = sentAt;
		request.FinishedAt = finishedAt;
		return request;
	}

	CostResult Known(double dollars)
	{
		CostResult cost;
		cost.Dollars = dollars;
		return cost;
	}

	CostResult Unknown()
	{
		CostResult cost;
		cost.UnknownModels.push_back("x");
		return cost;
	}
}

TEST_CASE("ContextLayersOf splits a request's context into cache read, cache write and uncached input")
{
	ApiRequest request;
	request.Usage.CacheRead = 800;
	request.Usage.CacheWrite5m = 20;
	request.Usage.CacheWrite1h = 10;
	request.Usage.Input = 5;
	request.Usage.Output = 99;

	const ContextLayers layers = ContextLayersOf(request);

	CHECK(layers[0] == 800.0);
	CHECK(layers[1] == 30.0);
	CHECK(layers[2] == 5.0);
}

TEST_CASE("StackedTops stacks every shown layer on the ones below it")
{
	const ContextLayers tops = StackedTops(kContextLayerValues, {true, true, true});

	CHECK(tops[0] == 800.0);
	CHECK(tops[1] == 830.0);
	CHECK(tops[2] == 835.0);
}

TEST_CASE("StackedTops leaves a hidden layer at the top of the one below and lowers the ones above")
{
	const ContextLayers tops = StackedTops(kContextLayerValues, {true, false, true});

	CHECK(tops[0] == 800.0);
	CHECK(tops[1] == 800.0);
	CHECK(tops[2] == 805.0);
}

TEST_CASE("StackedTops starts from zero when the bottom layer is hidden")
{
	const ContextLayers tops = StackedTops(kContextLayerValues, {false, true, true});

	CHECK(tops[0] == 0.0);
	CHECK(tops[1] == 30.0);
	CHECK(tops[2] == 35.0);
}

TEST_CASE("StackedTops is flat at zero with every layer hidden")
{
	const ContextLayers tops = StackedTops(kContextLayerValues, {false, false, false});

	CHECK(tops[0] == 0.0);
	CHECK(tops[1] == 0.0);
	CHECK(tops[2] == 0.0);
}

TEST_CASE("StackedTops stacks four layers and lowers the ones above a hidden one")
{
	const std::array<double, 4> tops = StackedTops(std::array<double, 4>{1.0, 2.0, 3.0, 4.0}, {true, false, true, true});

	CHECK(tops[0] == 1.0);
	CHECK(tops[1] == 1.0);
	CHECK(tops[2] == 4.0);
	CHECK(tops[3] == 8.0);
}

TEST_CASE("CostLayersOf splits a request's cost into cache read, cache write, input and output")
{
	ApiRequest request;
	request.Model = "m";
	request.Usage.Input = 1'000'000;
	request.Usage.Output = 1'000'000;
	request.Usage.CacheWrite5m = 1'000'000;
	request.Usage.CacheWrite1h = 1'000'000;
	request.Usage.CacheRead = 1'000'000;

	const Pricing pricing = OnePrice();
	const std::optional<CostLayers> layers = CostLayersOf(request, pricing);

	REQUIRE(layers);
	CHECK((*layers)[static_cast<std::size_t>(CostLayer::CacheRead)] == doctest::Approx(0.3));
	CHECK((*layers)[static_cast<std::size_t>(CostLayer::CacheWrite)] == doctest::Approx(9.75));
	CHECK((*layers)[static_cast<std::size_t>(CostLayer::Input)] == doctest::Approx(3.0));
	CHECK((*layers)[static_cast<std::size_t>(CostLayer::Output)] == doctest::Approx(15.0));

	const double layerTotal = (*layers)[0] + (*layers)[1] + (*layers)[2] + (*layers)[3];
	CHECK(layerTotal == doctest::Approx(pricing.CostOf({{UsageKey{request.Model, request.Fast}, request.Usage}}).Dollars));
}

TEST_CASE("CostLayersOf has nothing for a model with no price")
{
	ApiRequest request;
	request.Model = "unpriced";
	request.Usage.Input = 10;

	CHECK_FALSE(CostLayersOf(request, OnePrice()));
}

TEST_CASE("CumulativeCost runs flat across a request with no price instead of dropping to zero")
{
	const std::vector<double> cumulative = CumulativeCost({Known(1.0), Unknown(), Known(2.0)});

	REQUIRE(cumulative.size() == 3);
	CHECK(cumulative[0] == doctest::Approx(1.0));
	CHECK(cumulative[1] == doctest::Approx(1.0));
	CHECK(cumulative[2] == doctest::Approx(3.0));
}

TEST_CASE("CumulativeCost of no requests is empty")
{
	CHECK(CumulativeCost({}).empty());
}

TEST_CASE("ClockSeconds reads the response time and has nothing for one that does not parse")
{
	const std::optional<double> earlier = ClockSeconds(At("2026-09-24T10:00:00.000Z"));
	const std::optional<double> later = ClockSeconds(At("2026-09-24T10:01:00.500Z"));

	REQUIRE(earlier);
	REQUIRE(later);
	CHECK(*later - *earlier == doctest::Approx(60.5));
	CHECK_FALSE(ClockSeconds(At("")));
	CHECK_FALSE(ClockSeconds(At("not a time")));
}

TEST_CASE("IdleGapBeforeRequest is the time from the end of the previous response to this request being sent")
{
	using namespace std::chrono_literals;

	const std::optional<std::chrono::milliseconds> gap = IdleGapBeforeRequest(
		Span("2026-09-24T10:00:00.000Z", "2026-09-24T10:00:30.000Z"), Span("2026-09-24T10:01:00.000Z", "2026-09-24T10:01:10.000Z"));

	REQUIRE(gap);
	CHECK(*gap == 30s);
}

TEST_CASE("IdleGapBeforeRequest has nothing without the previous response's end and this request's sending")
{
	CHECK_FALSE(IdleGapBeforeRequest(Span("2026-09-24T10:00:00.000Z", ""), Span("2026-09-24T10:01:00.000Z", "2026-09-24T10:01:10.000Z")));
	CHECK_FALSE(IdleGapBeforeRequest(Span("2026-09-24T10:00:00.000Z", "2026-09-24T10:00:30.000Z"), Span("", "2026-09-24T10:01:10.000Z")));
	CHECK_FALSE(IdleGapBeforeRequest(Span("2026-09-24T10:00:00.000Z", "not a time"), Span("2026-09-24T10:01:00.000Z", "2026-09-24T10:01:10.000Z")));
}

TEST_CASE("IdleGapBeforeRequest is zero, never negative, when this request was sent before the previous response ended")
{
	using namespace std::chrono_literals;

	const std::optional<std::chrono::milliseconds> gap = IdleGapBeforeRequest(
		Span("2026-09-24T10:00:00.000Z", "2026-09-24T10:01:00.000Z"), Span("2026-09-24T10:00:59.000Z", "2026-09-24T10:01:10.000Z"));

	REQUIRE(gap);
	CHECK(*gap == 0ms);
}

TEST_CASE("CacheExpired holds past five minutes")
{
	using namespace std::chrono_literals;

	const ApiRequest previous;

	CHECK_FALSE(CacheExpired(previous, 4min + 59s));
	CHECK_FALSE(CacheExpired(previous, 5min));
	CHECK(CacheExpired(previous, 5min + 1s));
}

TEST_CASE("CacheExpired holds past an hour after a request that wrote to the hour-long cache")
{
	using namespace std::chrono_literals;

	ApiRequest previous;
	previous.Usage.CacheWrite1h = 10;

	CHECK_FALSE(CacheExpired(previous, 59min));
	CHECK(CacheExpired(previous, 61min));
}

TEST_CASE("OutputRate is output tokens per second of waiting")
{
	using namespace std::chrono_literals;

	ApiRequest request = At("", 2s);
	request.Usage.Output = 500;

	REQUIRE(OutputRate(request));
	CHECK(*OutputRate(request) == doctest::Approx(250.0));
}

TEST_CASE("OutputRate has nothing without a wait or with a zero one")
{
	using namespace std::chrono_literals;

	ApiRequest none;
	none.Usage.Output = 500;
	CHECK_FALSE(OutputRate(none));

	ApiRequest zero = At("", 0ms);
	zero.Usage.Output = 500;
	CHECK_FALSE(OutputRate(zero));
}

TEST_CASE("ClockBarWidth is a bar's share of the closest two requests, in any order")
{
	CHECK(ClockBarWidth({0.0, 10.0, 30.0}) == doctest::Approx(10.0 * kBarFraction));
	CHECK(ClockBarWidth({30.0, 0.0, 10.0}) == doctest::Approx(10.0 * kBarFraction));
}

TEST_CASE("ClockBarWidth falls back to a minute with fewer than two distinct times")
{
	CHECK(ClockBarWidth({}) == doctest::Approx(60.0));
	CHECK(ClockBarWidth({100.0}) == doctest::Approx(60.0));
	CHECK(ClockBarWidth({100.0, 100.0}) == doctest::Approx(60.0));
	CHECK(ClockBarWidth({100.0, 100.0, 103.0}) == doctest::Approx(3.0 * kBarFraction));
}
