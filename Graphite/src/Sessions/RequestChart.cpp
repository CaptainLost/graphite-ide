#include "Core/Base/Base.h"

#include "Sessions/RequestChart.h"

#include "Sessions/SessionSummary.h"

#include <algorithm>
#include <limits>

namespace Graphite {

	namespace {
		constexpr double kClockFallbackSeconds = 60.0;
	}

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

	std::optional<CostLayers> CostLayersOf(const ApiRequest& request, const Pricing& pricing)
	{
		const std::optional<BucketCosts> buckets = pricing.BucketsOf(UsageKey{request.Model, request.Fast}, request.Usage);

		if (!buckets)
			return std::nullopt;

		CostLayers layers{};
		layers[static_cast<std::size_t>(CostLayer::CacheRead)] = buckets->CacheRead;
		layers[static_cast<std::size_t>(CostLayer::CacheWrite)] = buckets->CacheWrite;
		layers[static_cast<std::size_t>(CostLayer::Input)] = buckets->Input;
		layers[static_cast<std::size_t>(CostLayer::Output)] = buckets->Output;
		return layers;
	}

	std::vector<double> CumulativeCost(const std::vector<CostResult>& costs)
	{
		std::vector<double> total;
		total.reserve(costs.size());

		double sum = 0.0;

		for (const CostResult& cost : costs)
		{
			if (cost.IsKnown())
				sum += cost.Dollars;

			total.push_back(sum);
		}

		return total;
	}

	std::optional<double> ClockSeconds(const ApiRequest& request)
	{
		const auto at = ParseTimestamp(request.RespondedAt);

		if (!at)
			return std::nullopt;

		return static_cast<double>(at->time_since_epoch().count()) / 1000.0;
	}

	std::optional<std::chrono::milliseconds> GapBefore(const ApiRequest& previous, const ApiRequest& request)
	{
		const auto before = ParseTimestamp(previous.RespondedAt);
		const auto after = ParseTimestamp(request.RespondedAt);

		if (!before || !after || !request.Waited)
			return std::nullopt;

		const std::chrono::milliseconds gap = (*after - *before) - *request.Waited;
		return std::max(gap, std::chrono::milliseconds{0});
	}

	bool CacheExpired(const ApiRequest& previous, std::chrono::milliseconds gap)
	{
		const std::chrono::milliseconds lifetime =
			previous.Usage.CacheWrite1h > 0 ? std::chrono::milliseconds{std::chrono::hours{1}}
											: std::chrono::milliseconds{std::chrono::minutes{5}};
		return gap > lifetime;
	}

	std::optional<double> OutputRate(const ApiRequest& request)
	{
		if (!request.Waited || request.Waited->count() <= 0)
			return std::nullopt;

		return static_cast<double>(request.Usage.Output) / (static_cast<double>(request.Waited->count()) / 1000.0);
	}

	double ClockBarWidth(std::vector<double> times)
	{
		std::sort(times.begin(), times.end());

		double closest = std::numeric_limits<double>::infinity();

		for (std::size_t index = 1; index < times.size(); ++index)
		{
			const double distance = times[index] - times[index - 1];

			if (distance > 0.0)
				closest = std::min(closest, distance);
		}

		return closest == std::numeric_limits<double>::infinity() ? kClockFallbackSeconds : closest * kBarFraction;
	}
}
