#pragma once

#include "Sessions/Pricing.h"
#include "Sessions/Requests/RequestTimeline.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <optional>
#include <vector>

namespace Graphite {

	enum class ContextLayer
	{
		CacheRead,
		CacheWrite,
		Uncached
	};

	inline constexpr std::size_t kContextLayers = 3;

	using ContextLayers = std::array<double, kContextLayers>;

	ContextLayers ContextLayersOf(const ApiRequest& request);

	template<std::size_t LayerCount>
	std::array<double, LayerCount> StackedTops(const std::array<double, LayerCount>& layerValues,
											   const std::array<bool, LayerCount>& shownLayers)
	{
		std::array<double, LayerCount> tops{};
		double stackedBelow = 0.0;

		for (std::size_t layer = 0; layer < LayerCount; ++layer)
		{
			if (shownLayers[layer])
			{
				stackedBelow += layerValues[layer];
			}

			tops[layer] = stackedBelow;
		}

		return tops;
	}

	enum class CostLayer
	{
		CacheRead,
		CacheWrite,
		Input,
		Output
	};

	inline constexpr std::size_t kCostLayers = 4;

	using CostLayers = std::array<double, kCostLayers>;

	std::optional<CostLayers> CostLayersOf(const ApiRequest& request, const Pricing& pricing);

	std::vector<double> CumulativeCost(const std::vector<CostResult>& costs);

	std::optional<double> ClockSeconds(const ApiRequest& request);

	std::optional<std::chrono::milliseconds> IdleGapBeforeRequest(const ApiRequest& previous, const ApiRequest& request);

	bool CacheExpired(const ApiRequest& previous, std::chrono::milliseconds gap);

	std::optional<double> OutputRate(const ApiRequest& request);

	inline constexpr double kBarFraction = 0.67;

	double ClockBarWidth(std::vector<double> times);
}
