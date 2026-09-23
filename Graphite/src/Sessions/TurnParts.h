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
		// One text from Claude.
		Reply,

		// A run of work between two replies: tool calls, thinking that has text, unknown blocks.
		Steps,

		// Text Claude Code injected into the conversation.
		Injected,

		// A message the user typed while Claude was working.
		Queued
	};

	struct TurnPart
	{
		PartKind Kind = PartKind::Reply;

		// Indices into Turn::Items, in order. Empty thinking is never among them.
		std::vector<std::size_t> Items;

		// For Steps: each tool name and how often it ran, most frequent first, ties by first use.
		std::vector<std::pair<std::string, std::size_t>> ToolCounts;
		std::size_t Failures = 0;
		std::chrono::milliseconds Duration{0};

		// The requests whose last visible item is in this part, and what they used.
		UsageByModel Usage;
		std::size_t Requests = 0;

		// Those requests' keys, in the order they first appeared in the turn.
		std::vector<std::string> RequestKeys;
	};

	std::vector<TurnPart> SplitTurn(const Turn& turn, const std::vector<ToolCall>& tools);

	// When the part last did anything: the latest of its items' times and, for a tool, when
	// it finished. A run whose last tool ran for fifteen minutes was busy for all of them, so
	// a pause is measured from here rather than from when that tool was called. Empty when
	// no time in the part parses.
	std::string LastActivity(const Turn& turn, const TurnPart& part, const std::vector<ToolCall>& tools);

	constexpr std::chrono::minutes kGapThreshold{2};

	// The pause between two timestamps when it is longer than kGapThreshold.
	std::optional<std::chrono::milliseconds> GapBetween(std::string_view earlier, std::string_view later);
}
