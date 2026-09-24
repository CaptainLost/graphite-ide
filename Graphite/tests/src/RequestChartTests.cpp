#include <doctest/doctest.h>

#include "Sessions/RequestChart.h"

#include <chrono>

using namespace Graphite;

namespace {

	// read, write, uncached
	constexpr ContextLayers kLayers = {800.0, 30.0, 5.0};

	std::chrono::local_seconds At(int year, unsigned month, unsigned day, int hours, int minutes, int seconds)
	{
		const std::chrono::local_days date{std::chrono::year{year} / std::chrono::month{month} / std::chrono::day{day}};
		return date + std::chrono::hours{hours} + std::chrono::minutes{minutes} + std::chrono::seconds{seconds};
	}
}

TEST_CASE("LayersOf splits a request's context into cache read, cache write and uncached input")
{
	ApiRequest request;
	request.Usage.CacheRead = 800;
	request.Usage.CacheWrite5m = 20;
	request.Usage.CacheWrite1h = 10;
	request.Usage.Input = 5;
	request.Usage.Output = 99;

	const ContextLayers layers = LayersOf(request);

	CHECK(layers[0] == 800.0);
	CHECK(layers[1] == 30.0);
	CHECK(layers[2] == 5.0);
}

TEST_CASE("StackedTops stacks every shown layer on the ones below it")
{
	const ContextLayers tops = StackedTops(kLayers, {true, true, true});

	CHECK(tops[0] == 800.0);
	CHECK(tops[1] == 830.0);
	CHECK(tops[2] == 835.0);
}

TEST_CASE("StackedTops leaves a hidden layer at the top of the one below and lowers the ones above")
{
	const ContextLayers tops = StackedTops(kLayers, {true, false, true});

	CHECK(tops[0] == 800.0);
	CHECK(tops[1] == 800.0);
	CHECK(tops[2] == 805.0);
}

TEST_CASE("StackedTops starts from zero when the bottom layer is hidden")
{
	const ContextLayers tops = StackedTops(kLayers, {false, true, true});

	CHECK(tops[0] == 0.0);
	CHECK(tops[1] == 30.0);
	CHECK(tops[2] == 35.0);
}

TEST_CASE("StackedTops is flat at zero with every layer hidden")
{
	const ContextLayers tops = StackedTops(kLayers, {false, false, false});

	CHECK(tops[0] == 0.0);
	CHECK(tops[1] == 0.0);
	CHECK(tops[2] == 0.0);
}

TEST_CASE("FormatLocalTime writes each style with zero-padded fields")
{
	const auto time = At(2026, 9, 3, 8, 5, 7);

	CHECK(FormatLocalTime(time, ClockStyle::Time) == "08:05:07");
	CHECK(FormatLocalTime(time, ClockStyle::MonthDayTime) == "09-03 08:05:07");
	CHECK(FormatLocalTime(time, ClockStyle::DateTime) == "2026-09-03 08:05:07");
	CHECK(FormatLocalTime(time, ClockStyle::DateTimeWeekday) == "2026-09-03 08:05:07 (Thursday)");
}

TEST_CASE("FormatLocalTime names the weekday in English, Sunday included")
{
	CHECK(FormatLocalTime(At(2026, 9, 23, 18, 52, 33), ClockStyle::DateTimeWeekday) ==
		  "2026-09-23 18:52:33 (Wednesday)");
	CHECK(FormatLocalTime(At(2026, 9, 27, 0, 0, 0), ClockStyle::DateTimeWeekday) == "2026-09-27 00:00:00 (Sunday)");
	CHECK(FormatLocalTime(At(2026, 9, 26, 23, 59, 59), ClockStyle::DateTimeWeekday) ==
		  "2026-09-26 23:59:59 (Saturday)");
}
