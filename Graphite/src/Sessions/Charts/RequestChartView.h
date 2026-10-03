#pragma once

#include "Sessions/Charts/ChartAxes.h"
#include "Sessions/Conversation/ConversationView.h"
#include "Sessions/LocalClock.h"
#include "Sessions/Pricing.h"
#include "Sessions/RequestChart.h"
#include "Sessions/RequestTimeline.h"

#include <imgui.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	// What RequestsView hands the chart each frame: the requests, and what the view already
	// worked out for each of them, by position in GetRequests().
	struct RequestChartInput
	{
		const RequestTimeline& Timeline;
		const std::vector<CostResult>& Costs;
		const std::vector<std::string>& ToolLabels;

		// Whether the table's filters let the request through. A position past the end passes:
		// the filters may not have run yet on the frame a request arrives.
		const std::vector<bool>& Passes;

		std::optional<std::size_t> Selected;
		std::uint64_t Generation = 0;
	};

	// What the chart stacks: the context by cache layer, or the time spent waiting and between
	// requests. What the requests cost is charted in the Cost tab, by CostView.
	enum class ChartMode
	{
		Context,
		Time
	};

	// What the requests are placed along: their number, or the time they were answered.
	enum class ChartAxis
	{
		Index,
		Clock
	};

	// The chart above the Requests table: one stacked bar per request, a line on a second axis
	// where the mode has one, the selected request's column banded, requests the filters leave
	// out dimmed, and cache rebuilds marked.
	class RequestChartView
	{
	public:
		// Draws the mode bar and the chart; returns the position of a request clicked on it.
		std::optional<std::size_t> Draw(const RequestChartInput& input, const ConversationContext& context);

	private:
		// Up to the most layers any mode stacks: the context's three, as many as the time has.
		static constexpr std::size_t kMaxLayers = kContextLayers;

		using Layers = std::array<double, kMaxLayers>;
		using Shown = std::array<bool, kMaxLayers>;

		void Rebuild(const RequestChartInput& input);
		void DrawModeBar();
		void CollectPoints();
		Layers LayerValues(std::size_t position) const;
		double LineValue(std::size_t position) const;
		void Stack(const RequestChartInput& input, const Shown& shown);
		void DrawSelectionBand(std::optional<std::size_t> selected, double slot, ImU32 accent) const;
		void DrawRebuildMarkers(const RequestChartInput& input, ImU32 colour) const;
		std::size_t NearestPoint(double x) const;
		void DrawTooltip(const RequestChartInput& input, std::size_t position, const ConversationContext& context);

		ChartMode m_Mode = ChartMode::Context;
		ChartAxis m_Axis = ChartAxis::Index;

		// By position in GetRequests(), worked out again whenever the timeline or the reading of
		// the transcript changes: each request's number and context layers.
		std::vector<double> m_Index;

		// When each request was answered, in UNIX seconds; NaN for one whose time does not
		// parse, which has no place on the clock axis. And the bars' width on that axis.
		std::vector<double> m_Times;
		double m_ClockBarWidth = 60.0;

		std::vector<ContextLayers> m_Context;

		// Each request's wait for the model in seconds (zero without one), the gap before it
		// and whether that gap outlived the cache, and its output tokens per second (NaN
		// without a wait).
		std::vector<double> m_Wait;
		std::vector<std::optional<std::chrono::milliseconds>> m_Gaps;
		std::vector<bool> m_Expired;
		std::vector<double> m_Rate;

		std::uint64_t m_BuiltVersion = UINT64_MAX;
		std::uint64_t m_BuiltGeneration = UINT64_MAX;
		std::size_t m_BuiltCount = 0;

		// This frame's points — the positions with a place on the current axis — their x, the
		// top of each layer for the ones the filters pass and for the ones they leave out (zero
		// for the other kind, so the two sets never overlap), the top of the whole stack, and
		// the line's value.
		std::vector<std::size_t> m_Points;
		std::vector<double> m_Xs;
		std::array<std::vector<double>, kMaxLayers> m_PassTops;
		std::array<std::vector<double>, kMaxLayers> m_FailTops;
		std::vector<double> m_StackTop;
		std::vector<double> m_Line;
		double m_BarWidth = kBarFraction;

		// The requests axis: fitted to every request as they arrive, until the reader moves it.
		FollowingAxis m_XAxis;

		LocalClock m_LocalClock;
	};
}
