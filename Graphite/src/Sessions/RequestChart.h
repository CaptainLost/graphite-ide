#pragma once

#include "Sessions/RequestTimeline.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <string>

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

	// How high each layer's bar reaches when only the layers `shown` are stacked, bottom up and
	// indexed by ContextLayer. A hidden layer adds nothing: its top is the top of the layer
	// under it, so a bar drawn for it would be covered exactly, and the layers above it sit
	// lower by what it held rather than floating over a gap.
	ContextLayers StackedTops(const ContextLayers& layers, const std::array<bool, kContextLayers>& shown);

	// How a wall-clock time is written: the time alone, the time with the month and day for a
	// row where the day changes, the whole date, or the whole date and the day of the week.
	enum class ClockStyle
	{
		Time,
		MonthDayTime,
		DateTime,
		DateTimeWeekday
	};

	// "18:52:33", "09-23 18:52:33", "2026-09-23 18:52:33", "2026-09-23 18:52:33 (Wednesday)".
	// The time is already on the reader's clock; this only writes it.
	std::string FormatLocalTime(std::chrono::local_seconds time, ClockStyle style);
}
