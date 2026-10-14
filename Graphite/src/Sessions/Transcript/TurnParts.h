#pragma once

#include "Sessions/Pricing.h"
#include "Sessions/Session.h"

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Graphite {

	enum class PartKind
	{
		Reply,
		Steps,
		Injected,
		Queued,
		Compaction
	};

	struct TurnPart
	{
		PartKind Kind = PartKind::Reply;

		std::vector<std::size_t> Items;

		std::vector<std::pair<std::string, std::size_t>> ToolCounts;
		std::size_t Failures = 0;
		std::chrono::milliseconds Duration{0};

		UsageByModel Usage;
		std::size_t Requests = 0;

		std::vector<std::string> RequestKeys;
	};

	std::vector<TurnPart> SplitTurn(const Turn& turn, const std::vector<ToolCall>& tools);

	std::string LastActivity(const Turn& turn, const TurnPart& part, const std::vector<ToolCall>& tools);

	constexpr std::chrono::minutes kGapThreshold{2};

	std::optional<std::chrono::milliseconds> GapBetween(std::string_view earlier, std::string_view later);

	std::optional<std::chrono::milliseconds> GapBefore(std::string_view earlier, const TurnItem& item);
}
