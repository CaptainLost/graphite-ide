#pragma once

#include "Sessions/Pricing.h"
#include "Sessions/Requests/RequestSort.h"
#include "Sessions/Requests/RequestTimeline.h"
#include "Sessions/Session.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	std::vector<std::size_t> TurnOfRequests(const std::vector<ApiRequest>& requests, const std::vector<Turn>& turns);

	struct TurnSummary
	{
		std::size_t Turn = 0;
		std::size_t Count = 0;

		std::size_t FirstIndex = 0;
		std::string FirstRespondedAt;
		std::string Model;
		bool Fast = false;
		bool MixedModels = false;

		bool HasUsage = false;

		std::uint64_t Context = 0;

		std::optional<std::int64_t> Delta;

		std::uint64_t CacheRead = 0;
		std::uint64_t ContextSum = 0;

		std::uint64_t Output = 0;
		CostResult Cost;

		std::optional<std::chrono::milliseconds> Waited;

		std::string StopReason;

		std::size_t Rebuilt = 0;
	};

	struct TurnGroup
	{
		TurnSummary Summary;

		std::vector<std::size_t> Positions;
	};

	std::vector<TurnGroup> GroupByTurn(const std::vector<std::size_t>& order, const std::vector<std::size_t>& turnOf,
									   const std::vector<ApiRequest>& requests, const std::vector<std::string>& prompts,
									   RequestColumn column, bool ascending, const Pricing& pricing);

}
