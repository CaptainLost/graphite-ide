#include "Core/Base/Base.h"

#include "Sessions/RequestChart.h"

namespace Graphite {

	ContextLayers LayersOf(const ApiRequest& request)
	{
		const TokenCounts& usage = request.Usage;

		ContextLayers layers{};
		layers[static_cast<std::size_t>(ContextLayer::CacheRead)] = static_cast<double>(usage.CacheRead);
		layers[static_cast<std::size_t>(ContextLayer::CacheWrite)] =
			static_cast<double>(usage.CacheWrite5m + usage.CacheWrite1h);
		layers[static_cast<std::size_t>(ContextLayer::Uncached)] = static_cast<double>(usage.Input);
		return layers;
	}

	ContextLayers StackedTops(const ContextLayers& layers, const std::array<bool, kContextLayers>& shown)
	{
		ContextLayers tops{};
		double below = 0.0;

		for (std::size_t layer = 0; layer < kContextLayers; ++layer)
		{
			if (shown[layer])
				below += layers[layer];

			tops[layer] = below;
		}

		return tops;
	}
}
