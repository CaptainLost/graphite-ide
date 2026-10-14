#include "Sessions/Charts/RequestChart.h"

#include "Core/Base/Base.h"

#include "Sessions/SessionSummary.h"

#include <algorithm>
#include <limits>

namespace Graphite {

	namespace {
		constexpr double kClockFallbackSeconds = 60.0;
		constexpr double kMillisecondsPerSecond = 1000.0;
		constexpr std::chrono::milliseconds kShortCacheLifetime = std::chrono::minutes{5};
		constexpr std::chrono::milliseconds kLongCacheLifetime = std::chrono::hours{1};
	}

	ContextLayers ContextLayersOf(const ApiRequest& request)
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
		{
			return std::nullopt;
		}

		CostLayers layers{};
		layers[static_cast<std::size_t>(CostLayer::CacheRead)] = buckets->CacheRead;
		layers[static_cast<std::size_t>(CostLayer::CacheWrite)] = buckets->CacheWrite;
		layers[static_cast<std::size_t>(CostLayer::Input)] = buckets->Input;
		layers[static_cast<std::size_t>(CostLayer::Output)] = buckets->Output;
		return layers;
	}

	std::vector<double> CumulativeCost(const std::vector<CostResult>& costs)
	{
		std::vector<double> cumulative;
		cumulative.reserve(costs.size());

		double runningTotal = 0.0;

		for (const CostResult& cost : costs)
		{
			if (cost.IsKnown())
			{
				runningTotal += cost.Dollars;
			}

			cumulative.push_back(runningTotal);
		}

		return cumulative;
	}

	std::optional<double> ClockSeconds(const ApiRequest& request)
	{
		const std::optional<std::chrono::sys_time<std::chrono::milliseconds>> respondedAt =
			ParseTimestamp(request.RespondedAt);

		if (!respondedAt)
		{
			return std::nullopt;
		}

		return static_cast<double>(respondedAt->time_since_epoch().count()) / kMillisecondsPerSecond;
	}

	std::optional<std::chrono::milliseconds> IdleGapBeforeRequest(const ApiRequest& previous, const ApiRequest& request)
	{
		const std::optional<std::chrono::sys_time<std::chrono::milliseconds>> finishedAt =
			ParseTimestamp(previous.FinishedAt);
		const std::optional<std::chrono::sys_time<std::chrono::milliseconds>> sentAt = ParseTimestamp(request.SentAt);

		if (!finishedAt || !sentAt)
		{
			return std::nullopt;
		}

		return std::max(std::chrono::milliseconds{*sentAt - *finishedAt}, std::chrono::milliseconds{0});
	}

	bool CacheExpired(const ApiRequest& previous, std::chrono::milliseconds gap)
	{
		const std::chrono::milliseconds lifetime =
			previous.Usage.CacheWrite1h > 0 ? kLongCacheLifetime : kShortCacheLifetime;
		return gap > lifetime;
	}

	std::optional<double> OutputRate(const ApiRequest& request)
	{
		if (!request.Waited || request.Waited->count() <= 0)
		{
			return std::nullopt;
		}

		return static_cast<double>(request.Usage.Output) / (static_cast<double>(request.Waited->count()) / kMillisecondsPerSecond);
	}

	double ClockBarWidth(std::vector<double> times)
	{
		std::sort(times.begin(), times.end());

		constexpr double kNoDistance = std::numeric_limits<double>::infinity();
		double closestDistance = kNoDistance;

		for (std::size_t index = 1; index < times.size(); ++index)
		{
			const double distance = times[index] - times[index - 1];

			if (distance > 0.0)
			{
				closestDistance = std::min(closestDistance, distance);
			}
		}

		return closestDistance == kNoDistance ? kClockFallbackSeconds : closestDistance * kBarFraction;
	}
}
