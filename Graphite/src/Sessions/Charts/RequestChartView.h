#pragma once

#include "Sessions/Charts/ChartAxes.h"
#include "Sessions/Conversation/ConversationView.h"
#include "Sessions/LocalClock.h"
#include "Sessions/Pricing.h"
#include "Sessions/Charts/RequestChart.h"
#include "Sessions/Requests/RequestTimeline.h"

#include <imgui.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	struct RequestChartInput
	{
		const RequestTimeline& Timeline;
		const std::vector<CostResult>& Costs;
		const std::vector<std::string>& ToolLabels;

		const std::vector<bool>& Passes;

		std::optional<std::size_t> Selected;
		std::uint64_t Generation = 0;
	};

	enum class ChartMode
	{
		Context,
		Time
	};

	enum class ChartAxis
	{
		Index,
		Clock
	};

	class RequestChartView
	{
	public:
		std::optional<std::size_t> Draw(const RequestChartInput& input, const ConversationContext& context);

	private:
		static constexpr std::size_t kMaxLayers = kContextLayers;

		static constexpr double kInitialClockBarWidth = 60.0;

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
		std::size_t NearestPoint(double plotX) const;
		void DrawTooltip(const RequestChartInput& input, std::size_t position, const ConversationContext& context);

		ChartMode m_Mode = ChartMode::Context;
		ChartAxis m_Axis = ChartAxis::Index;

		std::vector<double> m_RequestNumbers;

		std::vector<double> m_Times;
		double m_ClockBarWidth = kInitialClockBarWidth;

		std::vector<ContextLayers> m_ContextLayers;

		std::vector<double> m_WaitSeconds;
		std::vector<std::optional<std::chrono::milliseconds>> m_Gaps;
		std::vector<bool> m_GapOutlivedCache;
		std::vector<double> m_OutputRate;

		std::uint64_t m_BuiltVersion = UINT64_MAX;
		std::uint64_t m_BuiltGeneration = UINT64_MAX;
		std::size_t m_BuiltCount = 0;

		std::vector<std::size_t> m_Points;
		std::vector<double> m_AxisValues;
		std::array<std::vector<double>, kMaxLayers> m_PassTops;
		std::array<std::vector<double>, kMaxLayers> m_FailTops;
		std::vector<double> m_StackTop;
		std::vector<double> m_Line;
		double m_BarWidth = kBarFraction;

		FollowingAxis m_XAxis;

		LocalClock m_LocalClock;
	};
}
