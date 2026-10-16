#include "Sessions/Charts/RequestChartView.h"

#include "Core/Base/Base.h"

#include "Core/Base/Narrow.h"
#include "Core/Base/Strings.h"

#include "Sessions/Requests/RequestRow.h"
#include "Sessions/SessionDrawing.h"
#include "Sessions/Charts/StackedBarChart.h"
#include "Sessions/SessionText.h"
#include "Sessions/Transcript/ToolFacts.h"
#include "UI/TypographicMarks.h"
#include "UI/FittedText.h"

#include "Core/Base/ByteSize.h"
#include "Core/ImGui/ImGuiColors.h"

#include <imgui.h>
#include <implot.h>
#include <implot_internal.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <string>
#include <utility>

namespace Graphite {

	namespace {
		constexpr std::size_t kModelLabelBytes = 80;
		constexpr std::size_t kStopReasonBytes = 60;
		constexpr std::size_t kSentLabelBytes = 400;

		constexpr float kFilteredOutAlpha = 0.25f;

		// WARNING: On the clock axis a bar can be a fraction of a pixel wide, so the tooltip's hit test is
		// worked out in pixels against this floor instead of in plot units.
		constexpr float kMinimumHitEms = 0.25f;

		constexpr float kMinimumSentLabelEms = 6.0f;
		constexpr float kPaddingSides = 2.0f;
		constexpr float kMarkerSizeEms = 0.3f;
		constexpr float kMarkerGapEms = 0.15f;
		constexpr float kMarkerApexHeights = 1.5f;
		constexpr float kPlotTopMargin = 1.0f;
		constexpr double kMillisecondsPerSecond = 1000.0;
		constexpr double kHalf = 0.5;

		constexpr const char* kContextNames[kContextLayers] = {"Cache read", "Cache write", "Uncached input"};

		constexpr std::size_t kTimeLayers = 3;
		static_assert(kTimeLayers <= kContextLayers, "the chart keeps room for as many layers as the context has");
		constexpr std::size_t kWaitLayer = 0;
		constexpr std::size_t kGapLayer = 1;
		constexpr std::size_t kGapPastCacheLayer = 2;
		constexpr const char* kTimeNames[kTimeLayers] = {"Wait", "Gap", "Gap past cache life"};

		constexpr ImPlotScale kScaleKeepingSecondsVisibleBesideHours = ImPlotScale_SymLog;

		constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

		struct ChartLayer
		{
			const char* Name = nullptr;
			ImU32 Colour = 0;
		};

		struct ModeLayout
		{
			const char* Title = nullptr;
			std::array<ChartLayer, kContextLayers> Layers{};
			std::size_t LayerCount = 0;
			const char* BarCaption = nullptr;
			ImPlotScale BarScale = ImPlotScale_Linear;
			const char* LineName = nullptr;
			const char* LineCaption = nullptr;
			ImU32 LineColour = 0;
		};

		class LocalTimeOnTwentyFourHourClock
		{
		public:
			LocalTimeOnTwentyFourHourClock()
				: m_PlotStyle(ImPlot::GetStyle()), m_WasLocal(std::exchange(m_PlotStyle.UseLocalTime, true)), m_Was24Hour(std::exchange(m_PlotStyle.Use24HourClock, true))
			{
			}

			~LocalTimeOnTwentyFourHourClock()
			{
				m_PlotStyle.UseLocalTime = m_WasLocal;
				m_PlotStyle.Use24HourClock = m_Was24Hour;
			}

			LocalTimeOnTwentyFourHourClock(const LocalTimeOnTwentyFourHourClock&) = delete;
			LocalTimeOnTwentyFourHourClock& operator=(const LocalTimeOnTwentyFourHourClock&) = delete;

		private:
			ImPlotStyle& m_PlotStyle;
			bool m_WasLocal;
			bool m_Was24Hour;
		};

		ImU32 ContextColour(const SessionPalette& palette, std::size_t layer)
		{
			switch (static_cast<ContextLayer>(layer))
			{
			case ContextLayer::CacheRead:
				return palette.Web;
			case ContextLayer::CacheWrite:
				return palette.Edit;
			case ContextLayer::Uncached:
				return palette.Run;
			}

			return palette.Other;
		}

		ModeLayout LayoutOf(ChartMode mode, const SessionPalette& palette)
		{
			ModeLayout layout;

			switch (mode)
			{
			case ChartMode::Context:
				for (std::size_t layer = 0; layer < kContextLayers; ++layer)
				{
					layout.Layers[layer] = {kContextNames[layer], ContextColour(palette, layer)};
				}

				layout.Title = "Context per request";
				layout.LayerCount = kContextLayers;
				layout.BarCaption = "Tokens";
				break;
			case ChartMode::Time:
				layout.Layers[kWaitLayer] = {kTimeNames[kWaitLayer], palette.Web};
				layout.Layers[kGapLayer] = {kTimeNames[kGapLayer], palette.TextDisabled};
				layout.Layers[kGapPastCacheLayer] = {kTimeNames[kGapPastCacheLayer], palette.Failed};
				layout.Title = "Time per request";
				layout.LayerCount = kTimeLayers;
				layout.BarCaption = "Seconds";
				layout.BarScale = kScaleKeepingSecondsVisibleBesideHours;
				layout.LineName = "Output tokens/s";
				layout.LineCaption = "Tokens/s";
				layout.LineColour = palette.Accent;
				break;
			}

			return layout;
		}

		bool PassesFilters(const RequestChartInput& input, std::size_t position)
		{
			return position >= input.Passes.size() || input.Passes[position];
		}

		bool AnyLeftOutByFilters(const RequestChartInput& input, const std::vector<std::size_t>& points)
		{
			for (const std::size_t position : points)
			{
				if (!PassesFilters(input, position))
				{
					return true;
				}
			}

			return false;
		}

		const SentItem& LargestSentItem(const std::vector<SentItem>& sentItems)
		{
			const SentItem* largest = &sentItems.front();

			for (const SentItem& item : sentItems)
			{
				if (largest->Bytes < item.Bytes)
				{
					largest = &item;
				}
			}

			return *largest;
		}
	}

	std::optional<std::size_t> RequestChartView::Draw(const RequestChartInput& input, const ConversationContext& context)
	{
		if (input.Generation != m_BuiltGeneration || input.Timeline.GetVersion() != m_BuiltVersion)
		{
			Rebuild(input);
		}

		DrawModeBar();
		CollectPoints();

		const SessionPalette& palette = context.Palette;
		const ModeLayout layout = LayoutOf(m_Mode, palette);
		const int pointCount = Core::Narrow<int>(m_Points.size());
		std::optional<std::size_t> clicked;

		const LocalTimeOnTwentyFourHourClock plotTimeStyle;

		if (!BeginBarChart(layout.Title, "requestsChart"))
		{
			return clicked;
		}

		ImPlot::SetupAxis(ImAxis_X1, m_Axis == ChartAxis::Index ? "Request" : "Time answered", ImPlotAxisFlags_None);

		if (m_Axis == ChartAxis::Clock)
		{
			ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
		}
		else
		{
			// WARNING: SetupAxisScale(Time) sets a ConstraintRange that BeginPlot's per-frame axis reset does
			// not clear, so the number axis must be set back to Linear explicitly or it stays pinned to the
			// time window once the reader has visited the clock axis.
			ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Linear);
			SetupCountedAxis();
		}

		if (pointCount > 0)
		{
			const auto [lowestValue, highestValue] = std::minmax_element(m_AxisValues.begin(), m_AxisValues.end());
			const double slotPadding = m_BarWidth / kBarFraction * kHalf;
			m_XAxis.Setup(*lowestValue - slotPadding, *highestValue + slotPadding);
		}

		const bool lineFromZero = false;
		FinishBarChartSetup({.BarCaption = layout.BarCaption,
							 .BarScale = layout.BarScale,
							 .LineCaption = layout.LineName != nullptr ? layout.LineCaption : nullptr,
							 .LineFromZero = lineFromZero});

		Shown shown{};
		for (std::size_t layer = 0; layer < layout.LayerCount; ++layer)
		{
			shown[layer] = SeriesShown(layout.Layers[layer].Name);
		}

		Stack(input, shown);

		DrawSelectionBand(input.Selected, m_BarWidth / kBarFraction, palette.Accent);

		std::array<BarLayer, kMaxLayers> passing{};
		for (std::size_t layer = 0; layer < layout.LayerCount; ++layer)
		{
			const ChartLayer& chartLayer = layout.Layers[layer];
			passing[layer] = {chartLayer.Name, ImGui::ColorConvertU32ToFloat4(chartLayer.Colour), m_PassTops[layer]};
		}

		const std::span<const BarLayer> passingLayers{passing.data(), layout.LayerCount};
		PlotStackedBars(passingLayers, m_AxisValues, m_BarWidth);

		if (AnyLeftOutByFilters(input, m_Points))
		{
			std::array<std::string, kMaxLayers> ids;
			std::array<BarLayer, kMaxLayers> leftOut{};
			std::size_t leftOutCount = 0;

			for (std::size_t layer = 0; layer < layout.LayerCount; ++layer)
			{
				if (!shown[layer])
				{
					continue;
				}

				const ChartLayer& chartLayer = layout.Layers[layer];
				ids[leftOutCount] = std::string("##leftOut") + chartLayer.Name;
				leftOut[leftOutCount] = {
					ids[leftOutCount].c_str(),
					Core::WithAlphaScaled(ImGui::ColorConvertU32ToFloat4(chartLayer.Colour), kFilteredOutAlpha),
					m_FailTops[layer]};
				++leftOutCount;
			}

			const std::span<const BarLayer> leftOutLayers{leftOut.data(), leftOutCount};
			PlotStackedBars(leftOutLayers, m_AxisValues, m_BarWidth,
							ImPlotItemFlags_NoLegend);
		}

		if (layout.LineName != nullptr)
		{
			ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
			ImPlot::SetNextLineStyle(ImGui::ColorConvertU32ToFloat4(layout.LineColour));
			ImPlot::PlotLine(layout.LineName, m_AxisValues.data(), m_Line.data(), pointCount, ImPlotLineFlags_SkipNaN);
		}

		ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
		DrawRebuildMarkers(input, palette.Edit);

		// WARNING: ImPlot works out the legend's hover while drawing it in EndPlot, so this is last frame's,
		// which is where the mouse was when the button under it was pressed.
		const bool overLegend = ImPlot::GetCurrentPlot()->Items.Legend.Hovered;

		m_XAxis.Track();

		if (ImPlot::IsPlotHovered() && !overLegend)
		{
			if (pointCount > 0)
			{
				const double mousePlotX = ImPlot::GetPlotMousePos(ImAxis_X1, ImAxis_Y1).x;
				const std::size_t nearestPoint = NearestPoint(mousePlotX);
				const std::size_t position = m_Points[nearestPoint];

				if (ColumnClicked())
				{
					clicked = position;
				}

				const float mousePixelX = ImPlot::PlotToPixels(mousePlotX, 0.0, ImAxis_X1, ImAxis_Y1).x;
				const float nearestPixelX =
					ImPlot::PlotToPixels(m_AxisValues[nearestPoint], 0.0, ImAxis_X1, ImAxis_Y1).x;
				const float barEdgePixelX =
					ImPlot::PlotToPixels(m_AxisValues[nearestPoint] + m_BarWidth * kHalf, 0.0, ImAxis_X1, ImAxis_Y1).x;
				const float halfBarPixels = std::abs(barEdgePixelX - nearestPixelX);
				const float hitHalfPixels = std::max(halfBarPixels, ImGui::GetFontSize() * kMinimumHitEms);

				if (!ChartPanning() && std::abs(mousePixelX - nearestPixelX) <= hitHalfPixels)
				{
					DrawTooltip(input, position, context);
				}
			}
		}

		EndBarChart();
		return clicked;
	}

	void RequestChartView::Rebuild(const RequestChartInput& input)
	{
		const std::vector<ApiRequest>& requests = input.Timeline.GetRequests();
		const std::size_t requestCount = requests.size();

		const bool transcriptReadAgain = input.Generation != m_BuiltGeneration;

		if (transcriptReadAgain)
		{
			m_XAxis.Reset();
		}

		// WARNING: Every request is rebuilt, not only the last: a pass over a few thousand requests on the
		// frames the timeline changes costs less than tracking which ones could have changed.
		m_RequestNumbers.resize(requestCount);
		m_Times.resize(requestCount);
		m_ContextLayers.resize(requestCount);
		m_WaitSeconds.resize(requestCount);
		m_Gaps.resize(requestCount);
		m_GapOutlivedCache.resize(requestCount);
		m_OutputRate.resize(requestCount);

		for (std::size_t position = 0; position < requestCount; ++position)
		{
			const ApiRequest& request = requests[position];
			m_RequestNumbers[position] = static_cast<double>(request.Index);
			m_Times[position] = ClockSeconds(request).value_or(kNaN);
			m_ContextLayers[position] = ContextLayersOf(request);
			m_WaitSeconds[position] =
				request.Waited ? static_cast<double>(request.Waited->count()) / kMillisecondsPerSecond : 0.0;
			m_Gaps[position] =
				position > 0 ? IdleGapBeforeRequest(requests[position - 1], request) : std::nullopt;
			m_GapOutlivedCache[position] = m_Gaps[position] && CacheExpired(requests[position - 1], *m_Gaps[position]);
			m_OutputRate[position] = OutputRate(request).value_or(kNaN);
		}

		std::vector<double> timedSeconds;

		for (const double time : m_Times)
		{
			if (!std::isnan(time))
			{
				timedSeconds.push_back(time);
			}
		}

		m_ClockBarWidth = ClockBarWidth(std::move(timedSeconds));

		if (requestCount != m_BuiltCount)
		{
			m_XAxis.Refit();
			m_BuiltCount = requestCount;
		}

		m_BuiltVersion = input.Timeline.GetVersion();
		m_BuiltGeneration = input.Generation;
	}

	void RequestChartView::DrawModeBar()
	{
		constexpr const char* kModeLabels[] = {"Context", "Time"};

		int selectedMode = static_cast<int>(m_Mode);

		if (SegmentedChoice(kModeLabels, selectedMode))
		{
			m_Mode = static_cast<ChartMode>(selectedMode);
		}

		const char* axisLabel = m_Axis == ChartAxis::Index ? "X: Number###chartAxis" : "X: Clock###chartAxis";
		const bool hideTextAfterDoubleHash = true;
		const float axisButtonWidth =
			ImGui::CalcTextSize(axisLabel, nullptr, hideTextAfterDoubleHash).x + ImGui::GetStyle().FramePadding.x * kPaddingSides;

		ImGui::SameLine();
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(ImGui::GetContentRegionAvail().x - axisButtonWidth, 0.0f));

		if (ImGui::SmallButton(axisLabel))
		{
			m_Axis = m_Axis == ChartAxis::Index ? ChartAxis::Clock : ChartAxis::Index;
			m_XAxis.Reset();
		}

		ImGui::SetItemTooltip("Place the requests by number or by the time they were answered");
	}

	void RequestChartView::CollectPoints()
	{
		m_Points.clear();
		m_AxisValues.clear();

		const std::vector<double>& valuesAlongAxis = m_Axis == ChartAxis::Clock ? m_Times : m_RequestNumbers;

		for (std::size_t position = 0; position < valuesAlongAxis.size(); ++position)
		{
			if (std::isnan(valuesAlongAxis[position]))
			{
				continue;
			}

			m_Points.push_back(position);
			m_AxisValues.push_back(valuesAlongAxis[position]);
		}

		m_BarWidth = m_Axis == ChartAxis::Clock ? m_ClockBarWidth : kBarFraction;
	}

	RequestChartView::Layers RequestChartView::LayerValues(std::size_t position) const
	{
		Layers values{};

		switch (m_Mode)
		{
		case ChartMode::Context:
			for (std::size_t layer = 0; layer < kContextLayers; ++layer)
			{
				values[layer] = m_ContextLayers[position][layer];
			}
			break;
		case ChartMode::Time:
		{
			const double gapSeconds =
				m_Gaps[position] ? static_cast<double>(m_Gaps[position]->count()) / kMillisecondsPerSecond : 0.0;
			values[kWaitLayer] = m_WaitSeconds[position];
			values[m_GapOutlivedCache[position] ? kGapPastCacheLayer : kGapLayer] = gapSeconds;
			break;
		}
		}

		return values;
	}

	double RequestChartView::LineValue(std::size_t position) const
	{
		switch (m_Mode)
		{
		case ChartMode::Time:
			return m_OutputRate[position];
		case ChartMode::Context:
			break;
		}

		return kNaN;
	}

	void RequestChartView::Stack(const RequestChartInput& input, const Shown& shown)
	{
		const std::size_t pointCount = m_Points.size();

		for (std::vector<double>& tops : m_PassTops)
		{
			tops.assign(pointCount, 0.0);
		}

		for (std::vector<double>& tops : m_FailTops)
		{
			tops.assign(pointCount, 0.0);
		}

		m_StackTop.assign(pointCount, 0.0);
		m_Line.assign(pointCount, kNaN);

		for (std::size_t point = 0; point < pointCount; ++point)
		{
			const std::size_t position = m_Points[point];
			const Layers stackedTops = StackedTops(LayerValues(position), shown);
			const bool passes = PassesFilters(input, position);

			for (std::size_t layer = 0; layer < kMaxLayers; ++layer)
			{
				(passes ? m_PassTops : m_FailTops)[layer][point] = stackedTops[layer];
			}

			m_StackTop[point] = stackedTops[kMaxLayers - 1];
			m_Line[point] = LineValue(position);
		}
	}

	void RequestChartView::DrawSelectionBand(std::optional<std::size_t> selected, double slot, ImU32 accent) const
	{
		if (!selected)
		{
			return;
		}

		const std::vector<std::size_t>::const_iterator found = std::find(m_Points.begin(), m_Points.end(), *selected);

		if (found == m_Points.end())
		{
			return;
		}

		DrawColumnBand(m_AxisValues[static_cast<std::size_t>(found - m_Points.begin())], slot, accent);
	}

	void RequestChartView::DrawRebuildMarkers(const RequestChartInput& input, ImU32 colour) const
	{
		const std::vector<ApiRequest>& requests = input.Timeline.GetRequests();
		const float markerSize = std::round(ImGui::GetFontSize() * kMarkerSizeEms);
		const float markerGap = std::round(ImGui::GetFontSize() * kMarkerGapEms);
		ImDrawList* drawList = ImPlot::GetPlotDrawList();

		ImPlot::PushPlotClipRect();

		for (std::size_t point = 0; point < m_Points.size(); ++point)
		{
			const std::size_t position = m_Points[point];

			if (position >= requests.size() || !requests[position].CacheRebuilt)
			{
				continue;
			}

			const ImVec2 barTop = ImPlot::PlotToPixels(m_AxisValues[point], m_StackTop[point], ImAxis_X1, ImAxis_Y1);
			float tip = barTop.y - markerGap;
			float apexY = tip - markerSize * kMarkerApexHeights;

			// WARNING: kFitPadding leaves only a few pixels above the tallest bar, and none on SymLog. When the
			// apex would be clipped, the whole triangle is pulled down under the plot's top edge instead.
			const float plotTop = ImPlot::GetPlotPos().y + kPlotTopMargin;

			if (apexY < plotTop)
			{
				apexY = plotTop;
				tip = apexY + markerSize * kMarkerApexHeights;
			}

			drawList->AddTriangleFilled(ImVec2(barTop.x - markerSize, apexY), ImVec2(barTop.x + markerSize, apexY),
										ImVec2(barTop.x, tip), colour);
		}

		ImPlot::PopPlotClipRect();
	}

	std::size_t RequestChartView::NearestPoint(double plotX) const
	{
		std::size_t nearestPoint = 0;
		double nearestDistance = std::numeric_limits<double>::infinity();

		for (std::size_t point = 0; point < m_AxisValues.size(); ++point)
		{
			const double distance = std::abs(m_AxisValues[point] - plotX);

			if (distance < nearestDistance)
			{
				nearestDistance = distance;
				nearestPoint = point;
			}
		}

		return nearestPoint;
	}

	void RequestChartView::DrawTooltip(const RequestChartInput& input, std::size_t position, const ConversationContext& context)
	{
		const std::vector<ApiRequest>& requests = input.Timeline.GetRequests();

		if (position >= requests.size() || !ImGui::BeginTooltip())
		{
			return;
		}

		const ApiRequest& request = requests[position];
		const SessionPalette& palette = context.Palette;
		const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
		const float maxWidth = ImGui::GetFontSize() * kTooltipEms;
		const bool hasUsage = request.Usage.Total() > 0;

		std::string header = "#" + std::to_string(request.Index);

		if (const std::string answeredTime = m_LocalClock.Format(request.RespondedAt, ClockStyle::DateTime);
			!answeredTime.empty())
		{
			header += kMiddleDotSeparator;
			header += answeredTime;
		}

		if (!request.Model.empty())
		{
			header += kMiddleDotSeparator;
			header += SingleLineLabel(ModelLabel(request.Model, request.Fast), kModelLabelBytes);
		}

		ColouredText(ink, FitToWidth(header, maxWidth));

		if (hasUsage)
		{
			std::string contextLine = "Context " + FormatTokens(request.Context);

			if (request.ContextDelta)
			{
				contextLine += " (" + SignedTokens(*request.ContextDelta) + ")";
			}

			ColouredText(ink, contextLine);

			const ContextLayers contextLayers = ContextLayersOf(request);
			std::array<SwatchRow, kContextLayers> rows;

			for (std::size_t layer = 0; layer < kContextLayers; ++layer)
			{
				rows[layer] = {.Colour = ContextColour(palette, layer),
							   .Label = kContextNames[layer],
							   .Number = FormatTokens(static_cast<std::uint64_t>(contextLayers[layer]))};

				if (static_cast<ContextLayer>(layer) == ContextLayer::CacheRead && request.Context > 0)
				{
					rows[layer].Note = std::to_string(CachedPercent(request.Usage.CacheRead, request.Context)) + "%";
					rows[layer].NoteColour = palette.TextDisabled;
				}
			}

			DrawSwatchRows(rows, ink);
		}

		std::string outcome;

		if (hasUsage)
		{
			Core::AppendSeparated(outcome, kMiddleDotSeparator, "Out " + FormatTokens(request.Usage.Output));

			if (position < input.Costs.size())
			{
				Core::AppendSeparated(outcome, kMiddleDotSeparator, FormatCost(input.Costs[position]));
			}
		}

		if (request.Waited)
		{
			Core::AppendSeparated(outcome, kMiddleDotSeparator, "wait " + WaitText(*request.Waited));
		}

		if (!request.StopReason.empty())
		{
			Core::AppendSeparated(outcome, kMiddleDotSeparator, SingleLineLabel(request.StopReason, kStopReasonBytes));
		}

		if (!outcome.empty())
		{
			ColouredText(ink, FitToWidth(outcome, maxWidth));
		}

		if (position < m_Gaps.size() && m_Gaps[position])
		{
			const bool outlivedCache = m_GapOutlivedCache[position];

			std::string gapLine = "Gap before: " + WaitText(*m_Gaps[position]);

			if (outlivedCache)
			{
				gapLine += kEmDashSeparator;
				gapLine += "past the cache's life";
			}

			ColouredText(outlivedCache ? palette.Failed : palette.TextDisabled, gapLine);
		}

		if (position < input.ToolLabels.size() && !input.ToolLabels[position].empty())
		{
			ColouredText(palette.TextDisabled, FitToWidth("Tools: " + input.ToolLabels[position], maxWidth));
		}

		if (request.Sent.empty())
		{
			ColouredText(palette.TextDisabled, "Sent: nothing new");
		}
		else
		{
			const SentItem& largest = LargestSentItem(request.Sent);

			const std::size_t itemCount = request.Sent.size();
			std::string prefix = "Sent: " + Core::Counted(itemCount, "item");
			prefix += kEmDashSeparator;
			prefix += "largest: ";
			const std::string suffix = "  " + Core::FormatByteSize(largest.Bytes);

			const float labelRoom =
				std::max(maxWidth - ImGui::CalcTextSize(prefix.c_str()).x - ImGui::CalcTextSize(suffix.c_str()).x,
						 ImGui::GetFontSize() * kMinimumSentLabelEms);

			ColouredText(palette.TextDisabled,
						 prefix + FitToWidth(SingleLineLabel(largest.Label, kSentLabelBytes), labelRoom) + suffix);
		}

		ImGui::EndTooltip();
	}
}
