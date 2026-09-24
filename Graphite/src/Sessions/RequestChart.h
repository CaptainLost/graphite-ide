#pragma once

#include "Sessions/Pricing.h"
#include "Sessions/RequestTimeline.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <optional>
#include <vector>

namespace Graphite {

	// The layers a request's context is stacked from in the Requests chart, bottom up: what was
	// read from the cache, what was written to it, and what went in uncached on top.
	enum class ContextLayer
	{
		CacheRead,
		CacheWrite,
		Uncached
	};

	inline constexpr std::size_t kContextLayers = 3;

	using ContextLayers = std::array<double, kContextLayers>;

	// The request's tokens in each layer, indexed by ContextLayer. They add up to its Context.
	ContextLayers LayersOf(const ApiRequest& request);

	// How high each layer's bar reaches when only the layers `shown` are stacked, bottom up. A
	// hidden layer adds nothing: its top is the top of the layer under it, so a bar drawn for it
	// would be covered exactly, and the layers above it sit lower by what it held rather than
	// floating over a gap.
	template <std::size_t N>
	std::array<double, N> StackedTops(const std::array<double, N>& layers, const std::array<bool, N>& shown)
	{
		std::array<double, N> tops{};
		double below = 0.0;

		for (std::size_t layer = 0; layer < N; ++layer)
		{
			if (shown[layer])
				below += layers[layer];

			tops[layer] = below;
		}

		return tops;
	}

	// The layers a request's cost is stacked from, bottom up: the three the context is made of,
	// in the context's order, and the output on top.
	enum class CostLayer
	{
		CacheRead,
		CacheWrite,
		Input,
		Output
	};

	inline constexpr std::size_t kCostLayers = 4;

	using CostLayers = std::array<double, kCostLayers>;

	// The request's cost in each layer, in dollars and indexed by CostLayer; nothing when its
	// model has no price. They add up to what Pricing::CostOf makes of the request.
	std::optional<CostLayers> CostLayersOf(const ApiRequest& request, const Pricing& pricing);

	// The session's cost up to and including each request. A request whose cost is not known
	// adds nothing, so the line runs flat across it rather than dropping to zero.
	std::vector<double> CumulativeCost(const std::vector<CostResult>& costs);

	// When the request was answered, in seconds since the UNIX epoch; nothing when its
	// timestamp does not parse.
	std::optional<double> ClockSeconds(const ApiRequest& request);

	// The time between the end of `previous` and `request` being sent: what the tools, or the
	// reader, took beyond the response. Nothing when either timestamp does not parse; zero when
	// the request reads as sent first, which a clock set back or a rounded timestamp can make it.
	std::optional<std::chrono::milliseconds> GapBefore(const ApiRequest& previous, const ApiRequest& request);

	// Whether a gap this long after `previous` outlived the prompt cache: five minutes, or an
	// hour when `previous` wrote to the hour-long cache. A gap of exactly the lifetime has not.
	bool CacheExpired(const ApiRequest& previous, std::chrono::milliseconds gap);

	// Output tokens per second of waiting; nothing without a wait, or with one of zero.
	std::optional<double> OutputRate(const ApiRequest& request);

	// A bar's width as a share of the room one request has on the axis.
	inline constexpr double kBarFraction = 0.67;

	// A bar's width, in seconds, on a clock axis holding requests at `times`: its share of the
	// closest two distinct times, so no two bars overlap. A minute when there are fewer than two
	// distinct times, which leaves nothing to measure against.
	double ClockBarWidth(std::vector<double> times);
}
