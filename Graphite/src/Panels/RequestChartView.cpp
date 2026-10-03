#include "Core/Base/Base.h"

#include "Panels/RequestChartView.h"

#include "Panels/RequestRow.h"
#include "Panels/SessionDrawing.h"
#include "Panels/StackedBarChart.h"
#include "Sessions/SessionText.h"
#include "Sessions/ToolFacts.h"

#include "Core/Base/ByteSize.h"

#include <imgui.h>
#include <implot.h>
#include <implot_internal.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <utility>

namespace Graphite {

	namespace {
		constexpr std::size_t kToolNameBytes = 80;

		// How strongly a request the filters leave out is drawn.
		constexpr float kDimAlpha = 0.25f;

		// The tooltip's hit test's minimum size, in font sizes rather than in plot units: on the
		// clock axis a bar is kBarFraction of the smallest gap between responses, which on a
		// multi-hour session is a fraction of a pixel — too thin to point at. It is worked out
		// in pixels against this floor instead, as DrawColumnBand does for the selection band.
		constexpr float kMinHitEms = 0.25f;

		// The context's layers, indexed by ContextLayer; they name the legend entries too.
		constexpr const char* kContextNames[kContextLayers] = {"Cache read", "Cache write", "Uncached input"};

		// The time's layers, bottom up: the wait for the model, then the gap before the request —
		// drawn as one of two layers, by whether it outlived the cache, so the ones that did
		// stand out and can be hidden on their own. One of the two is always zero.
		constexpr std::size_t kTimeLayers = 3;
		static_assert(kTimeLayers <= kContextLayers, "the chart keeps room for as many layers as the context has");
		constexpr const char* kTimeNames[kTimeLayers] = {"Wait", "Gap", "Gap past cache life"};

		constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

		struct ChartLayer
		{
			const char* Name = nullptr;
			ImU32 Colour = 0;
		};

		// What a mode draws: its layers bottom up, the value axis they stand on, and the line on
		// the second axis, if it has one.
		struct ModeLayout
		{
			// Centred above the plot. Behind "###" in the plot's id, so a change of mode keeps
			// the plot, and with it the reader's view and the legend's state.
			const char* Title = nullptr;
			std::array<ChartLayer, kContextLayers> Layers{};
			std::size_t LayerCount = 0;
			const char* Y1Label = nullptr;
			ImPlotScale Y1Scale = ImPlotScale_Linear;
			const char* LineName = nullptr;
			const char* Y2Label = nullptr;
			ImU32 LineColour = 0;
		};

		ImVec4 Faded(ImU32 colour, float alpha)
		{
			ImVec4 faded = PlotColour(colour);
			faded.w *= alpha;
			return faded;
		}

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
					layout.Layers[layer] = {kContextNames[layer], ContextColour(palette, layer)};

				layout.Title = "Context per request";
				layout.LayerCount = kContextLayers;
				layout.Y1Label = "Tokens";
				break;
			case ChartMode::Time:
				layout.Layers[0] = {kTimeNames[0], palette.Web};
				layout.Layers[1] = {kTimeNames[1], palette.TextDisabled};
				layout.Layers[2] = {kTimeNames[2], palette.Failed};
				layout.Title = "Time per request";
				layout.LayerCount = kTimeLayers;

				// One gap of an hour would otherwise flatten every wait of a few seconds.
				layout.Y1Label = "Seconds";
				layout.Y1Scale = ImPlotScale_SymLog;
				layout.LineName = "Output tokens/s";
				layout.Y2Label = "Tokens/s";
				layout.LineColour = palette.Accent;
				break;
			}

			return layout;
		}
	}

	std::optional<std::size_t> RequestChartView::Draw(const RequestChartInput& input, const ConversationContext& context)
	{
		if (input.Generation != m_BuiltGeneration || input.Timeline.GetVersion() != m_BuiltVersion)
			Rebuild(input);

		DrawModeBar();
		CollectPoints();

		const SessionPalette& palette = context.Palette;
		const ModeLayout layout = LayoutOf(m_Mode, palette);
		const int count = static_cast<int>(m_Points.size());
		std::optional<std::size_t> clicked;

		// The clock axis reads in the reader's time zone and on a 24-hour clock, as the table's
		// Time column does. ImPlot keeps both in its global style, so they are set for this plot
		// and put back after it.
		ImPlotStyle& plotStyle = ImPlot::GetStyle();
		const bool wasLocal = std::exchange(plotStyle.UseLocalTime, true);
		const bool was24Hour = std::exchange(plotStyle.Use24HourClock, true);
		const auto restoreStyle = [&plotStyle, wasLocal, was24Hour] {
			plotStyle.UseLocalTime = wasLocal;
			plotStyle.Use24HourClock = was24Hour;
		};

		if (!BeginBarChart(layout.Title, "requestsChart"))
		{
			restoreStyle();
			return clicked;
		}

		// The requests axis is fitted to every request when they change, until the reader pans
		// or zooms it; the value axes follow whatever part of it is in view.
		ImPlot::SetupAxis(ImAxis_X1, m_Axis == ChartAxis::Index ? "Request" : "Time answered", ImPlotAxisFlags_None);

		if (m_Axis == ChartAxis::Clock)
		{
			ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
		}
		else
		{
			// SetupAxisScale(Time) sets ConstraintRange to a window of UNIX seconds
			// (IMPLOT_MIN_TIME..IMPLOT_MAX_TIME), and ImPlotAxis::Reset(), which BeginPlot runs
			// at the start of every frame, does not clear ConstraintRange. Left alone, the
			// number axis would stay pinned to that window — unable to pan below 0 — for the
			// rest of the session once the reader had visited the clock axis once. Explicitly
			// setting Linear here puts it back: implot.cpp's SetupAxisScale takes the
			// ConstraintRange (-INFINITY, INFINITY) in its `default` branch, which every scale
			// other than Time, Log10 and SymLog falls into (verified in
			// vendor/implot/implot.cpp), so this alone is enough; no separate
			// SetupAxisLimitsConstraints call is needed.
			ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Linear);
			SetupCountedAxis();
		}

		if (count > 0)
		{
			const auto [low, high] = std::minmax_element(m_Xs.begin(), m_Xs.end());
			const double pad = m_BarWidth / kBarFraction * 0.5;
			m_XAxis.Setup(*low - pad, *high + pad);
		}

		// The line's axis, where the mode has one, is fitted to the line alone and padded at
		// both ends: a rate has no floor worth standing on.
		FinishBarChartSetup({.BarCaption = layout.Y1Label,
							 .BarScale = layout.Y1Scale,
							 .LineCaption = layout.LineName != nullptr ? layout.Y2Label : nullptr,
							 .LineFromZero = false});

		Shown shown{};
		for (std::size_t layer = 0; layer < layout.LayerCount; ++layer)
			shown[layer] = SeriesShown(layout.Layers[layer].Name);

		Stack(input, shown);

		// Under the bars: drawn into the plot's draw list before any of them.
		DrawSelectionBand(input.Selected, m_BarWidth / kBarFraction, palette.Accent);

		// The requests the filters let through, a layer each with its legend entry; then the
		// ones they leave out, a second, faded set of bars with no legend entry of their own,
		// left out with their layer.
		std::array<BarLayer, kMaxLayers> passing{};
		for (std::size_t layer = 0; layer < layout.LayerCount; ++layer)
		{
			const ChartLayer& spec = layout.Layers[layer];
			passing[layer] = {spec.Name, PlotColour(spec.Colour), m_PassTops[layer]};
		}

		PlotStackedBars(std::span(passing).first(layout.LayerCount), m_Xs, m_BarWidth);

		const bool anyLeftOut = std::any_of(m_Points.begin(), m_Points.end(), [&input](std::size_t position) { return position < input.Passes.size() && !input.Passes[position]; });

		if (anyLeftOut)
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

				const ChartLayer& spec = layout.Layers[layer];
				ids[leftOutCount] = std::string("##leftOut") + spec.Name;
				leftOut[leftOutCount] = {ids[leftOutCount].c_str(), Faded(spec.Colour, kDimAlpha),
										 m_FailTops[layer]};
				++leftOutCount;
			}

			PlotStackedBars(std::span(leftOut).first(leftOutCount), m_Xs, m_BarWidth,
							ImPlotItemFlags_NoLegend);
		}

		if (layout.LineName != nullptr)
		{
			ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
			ImPlot::SetNextLineStyle(PlotColour(layout.LineColour));
			ImPlot::PlotLine(layout.LineName, m_Xs.data(), m_Line.data(), count, ImPlotLineFlags_SkipNaN);
		}

		ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
		DrawRebuildMarkers(input, palette.Edit);

		// The legend's entries are buttons: a click there hides a series and must not also
		// select a request. ImPlot works out the legend's hover as it draws it in EndPlot, so
		// what is read here is the last frame's, which is where the mouse was when the button
		// under it was pressed.
		const bool overLegend = ImPlot::GetCurrentPlot()->Items.Legend.Hovered;

		m_XAxis.Track();

		// The click and the tooltip stay on the plot rectangle alone: the axis's tick area is
		// not where a bar is.
		if (ImPlot::IsPlotHovered() && !overLegend)
		{
			if (count > 0)
			{
				const double x = ImPlot::GetPlotMousePos(ImAxis_X1, ImAxis_Y1).x;
				const std::size_t nearest = NearestPoint(x);
				const std::size_t position = m_Points[nearest];

				// A click anywhere over a request's column selects it in the table: the bars are
				// too thin to be hit exactly once there are a few hundred of them.
				if (ColumnClicked())
				{
					clicked = position;
				}

				// The tooltip only over the bar itself, not the gap beside it, and not while the
				// chart is being panned under the mouse.
				// Compared in pixels, not plot units: on the clock axis the bar is a fraction
				// of a pixel wide, so half its width would almost never be hit. Floored at
				// kMinHitEms of the font size, same reasoning as the selection band above.
				const float mousePixelX = ImPlot::PlotToPixels(x, 0.0, ImAxis_X1, ImAxis_Y1).x;
				const float nearestPixelX = ImPlot::PlotToPixels(m_Xs[nearest], 0.0, ImAxis_X1, ImAxis_Y1).x;
				const float barEdgePixelX =
					ImPlot::PlotToPixels(m_Xs[nearest] + m_BarWidth * 0.5, 0.0, ImAxis_X1, ImAxis_Y1).x;
				const float halfBarPixels = std::abs(barEdgePixelX - nearestPixelX);
				const float hitHalfPixels = std::max(halfBarPixels, ImGui::GetFontSize() * kMinHitEms);

				if (!ChartPanning() && std::abs(mousePixelX - nearestPixelX) <= hitHalfPixels)
				{
					DrawTooltip(input, position, context);
				}
			}
		}

		EndBarChart();
		restoreStyle();
		return clicked;
	}

	void RequestChartView::Rebuild(const RequestChartInput& input)
	{
		const std::vector<ApiRequest>& requests = input.Timeline.GetRequests();
		const std::size_t count = requests.size();

		// Another reading of the transcript: where the reader had panned to named other requests,
		// so the view is fitted again even if the count of requests did not change below.
		if (input.Generation != m_BuiltGeneration)
			m_XAxis.Reset();

		// Every request, not only the last: a pass over a few thousand requests on the frames
		// the timeline changes costs less than keeping track of which ones could have.
		m_Index.resize(count);
		m_Times.resize(count);
		m_Context.resize(count);
		m_Wait.resize(count);
		m_Gaps.resize(count);
		m_Expired.resize(count);
		m_Rate.resize(count);

		for (std::size_t position = 0; position < count; ++position)
		{
			const ApiRequest& request = requests[position];
			m_Index[position] = static_cast<double>(request.Index);
			m_Times[position] = ClockSeconds(request).value_or(kNaN);
			m_Context[position] = LayersOf(request);
			m_Wait[position] = request.Waited ? static_cast<double>(request.Waited->count()) / 1000.0 : 0.0;
			m_Gaps[position] = position > 0 ? GapBefore(requests[position - 1], request) : std::nullopt;
			m_Expired[position] = m_Gaps[position] && CacheExpired(requests[position - 1], *m_Gaps[position]);
			m_Rate[position] = OutputRate(request).value_or(kNaN);
		}

		std::vector<double> timed;
		std::copy_if(m_Times.begin(), m_Times.end(), std::back_inserter(timed), [](double time) { return !std::isnan(time); });
		m_ClockBarWidth = ClockBarWidth(std::move(timed));

		if (count != m_BuiltCount)
		{
			m_XAxis.Refit();
			m_BuiltCount = count;
		}

		m_BuiltVersion = input.Timeline.GetVersion();
		m_BuiltGeneration = input.Generation;
	}

	void RequestChartView::DrawModeBar()
	{
		// In ChartMode's order.
		constexpr const char* kModes[] = {"Context", "Time"};

		int mode = static_cast<int>(m_Mode);

		if (SegmentedChoice(kModes, mode))
			m_Mode = static_cast<ChartMode>(mode);

		// At the row's right end: what the requests are placed along. Switching fits the axis
		// again, since a range in request numbers means nothing in seconds.
		const char* axisLabel = m_Axis == ChartAxis::Index ? "X: Number###chartAxis" : "X: Clock###chartAxis";
		const float axisWidth = ImGui::CalcTextSize(axisLabel, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;

		ImGui::SameLine();
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(ImGui::GetContentRegionAvail().x - axisWidth, 0.0f));

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
		m_Xs.clear();

		const std::vector<double>& xs = m_Axis == ChartAxis::Clock ? m_Times : m_Index;

		for (std::size_t position = 0; position < xs.size(); ++position)
		{
			if (std::isnan(xs[position]))
				continue;

			m_Points.push_back(position);
			m_Xs.push_back(xs[position]);
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
				values[layer] = m_Context[position][layer];
			break;
		case ChartMode::Time:
		{
			const double gap = m_Gaps[position] ? static_cast<double>(m_Gaps[position]->count()) / 1000.0 : 0.0;
			values[0] = m_Wait[position];
			values[m_Expired[position] ? 2 : 1] = gap;
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
			return m_Rate[position];
		case ChartMode::Context:
			break;
		}

		return kNaN;
	}

	void RequestChartView::Stack(const RequestChartInput& input, const Shown& shown)
	{
		const std::size_t count = m_Points.size();

		for (std::vector<double>& tops : m_PassTops)
			tops.assign(count, 0.0);

		for (std::vector<double>& tops : m_FailTops)
			tops.assign(count, 0.0);

		m_StackTop.assign(count, 0.0);
		m_Line.assign(count, kNaN);

		for (std::size_t point = 0; point < count; ++point)
		{
			const std::size_t position = m_Points[point];
			const Layers tops = StackedTops(LayerValues(position), shown);
			const bool passes = position >= input.Passes.size() || input.Passes[position];

			for (std::size_t layer = 0; layer < kMaxLayers; ++layer)
				(passes ? m_PassTops : m_FailTops)[layer][point] = tops[layer];

			m_StackTop[point] = tops[kMaxLayers - 1];
			m_Line[point] = LineValue(position);
		}
	}

	void RequestChartView::DrawSelectionBand(std::optional<std::size_t> selected, double slot, ImU32 accent) const
	{
		if (!selected)
			return;

		const auto found = std::find(m_Points.begin(), m_Points.end(), *selected);

		if (found == m_Points.end())
			return;

		DrawColumnBand(m_Xs[static_cast<std::size_t>(found - m_Points.begin())], slot, accent);
	}

	void RequestChartView::DrawRebuildMarkers(const RequestChartInput& input, ImU32 colour) const
	{
		const std::vector<ApiRequest>& requests = input.Timeline.GetRequests();
		const float size = std::round(ImGui::GetFontSize() * 0.3f);
		const float gap = std::round(ImGui::GetFontSize() * 0.15f);
		ImDrawList* list = ImPlot::GetPlotDrawList();

		// A small triangle pointing down at the top of the bar, whatever the mode stacks: a
		// rebuild is what explains a jump in the context and the wait alike.
		ImPlot::PushPlotClipRect();

		for (std::size_t point = 0; point < m_Points.size(); ++point)
		{
			const std::size_t position = m_Points[point];

			if (position >= requests.size() || !requests[position].CacheRebuilt)
				continue;

			const ImVec2 top = ImPlot::PlotToPixels(m_Xs[point], m_StackTop[point], ImAxis_X1, ImAxis_Y1);
			float tip = top.y - gap;
			float apexY = tip - size * 1.5f;

			// kFitPadding buys only a few pixels above the tallest bar — nothing at all on
			// SymLog, where the fit is dominated by the axis's low end. When that headroom runs
			// out before the marker's apex does, the whole triangle is pulled down to fit under
			// the plot's top edge instead of being clipped there: it now overlaps the bar it
			// marks, but still points down at it.
			const float plotTop = ImPlot::GetPlotPos().y + 1.0f;

			if (apexY < plotTop)
			{
				apexY = plotTop;
				tip = apexY + size * 1.5f;
			}

			list->AddTriangleFilled(ImVec2(top.x - size, apexY), ImVec2(top.x + size, apexY), ImVec2(top.x, tip),
									colour);
		}

		ImPlot::PopPlotClipRect();
	}

	std::size_t RequestChartView::NearestPoint(double x) const
	{
		std::size_t nearest = 0;
		double distance = std::numeric_limits<double>::infinity();

		for (std::size_t point = 0; point < m_Xs.size(); ++point)
		{
			const double away = std::abs(m_Xs[point] - x);

			if (away < distance)
			{
				distance = away;
				nearest = point;
			}
		}

		return nearest;
	}

	void RequestChartView::DrawTooltip(const RequestChartInput& input, std::size_t position, const ConversationContext& context)
	{
		const std::vector<ApiRequest>& requests = input.Timeline.GetRequests();

		if (position >= requests.size() || !ImGui::BeginTooltip())
			return;

		const ApiRequest& request = requests[position];
		const SessionPalette& palette = context.Palette;
		const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
		const float maxWidth = ImGui::GetFontSize() * kTooltipEms;
		const bool hasUsage = request.Usage.Total() > 0;

		std::string header = "#" + std::to_string(request.Index);

		if (const std::string time = m_LocalClock.Format(request.RespondedAt, ClockStyle::DateTime); !time.empty())
			header += kDot + time;

		if (!request.Model.empty())
			header += kDot + SingleLineLabel(ModelLabel(request.Model, request.Fast), kToolNameBytes);

		ColouredText(ink, Fitted(header, maxWidth));

		// The context and its layers, each after a swatch in its bar's colour, the numbers lined
		// up on the right of one column. A part the request does not carry is left out, as in
		// its line.
		if (hasUsage)
		{
			std::string total = "Context " + FormatTokens(request.Context);

			if (request.ContextDelta)
				total += " (" + SignedTokens(*request.ContextDelta) + ")";

			ColouredText(ink, total);

			const ContextLayers layers = LayersOf(request);
			std::array<SwatchRow, kContextLayers> rows;

			for (std::size_t layer = 0; layer < kContextLayers; ++layer)
			{
				rows[layer] = {.Colour = ContextColour(palette, layer),
							   .Label = kContextNames[layer],
							   .Number = FormatTokens(static_cast<std::uint64_t>(layers[layer]))};

				// The cached share, on the layer it is the share of.
				if (static_cast<ContextLayer>(layer) == ContextLayer::CacheRead && request.Context > 0)
				{
					rows[layer].Note = std::to_string(CachedPercent(request.Usage.CacheRead, request.Context)) + "%";
					rows[layer].NoteColour = palette.TextDisabled;
				}
			}

			DrawSwatchRows(rows, ink);
		}

		std::string outcome;
		const auto add = [&outcome](const std::string& part) { outcome += (outcome.empty() ? "" : kDot) + part; };

		if (hasUsage)
		{
			add("Out " + FormatTokens(request.Usage.Output));

			if (position < input.Costs.size())
				add(FormatCost(input.Costs[position]));
		}

		if (request.Waited)
			add("wait " + WaitText(*request.Waited));

		if (!request.StopReason.empty())
			add(SingleLineLabel(request.StopReason, 60));

		if (!outcome.empty())
			ColouredText(ink, Fitted(outcome, maxWidth));

		// What came before the request: the tools' or the reader's time, and whether the cache
		// was still there when it was sent.
		if (position < m_Gaps.size() && m_Gaps[position])
		{
			const bool expired = m_Expired[position];

			// "—" is U+2014.
			ColouredText(expired ? palette.Failed : palette.TextDisabled,
						 "Gap before: " + WaitText(*m_Gaps[position]) + (expired ? " \xe2\x80\x94 past the cache's life" : ""));
		}

		if (position < input.ToolLabels.size() && !input.ToolLabels[position].empty())
			ColouredText(palette.TextDisabled, Fitted("Tools: " + input.ToolLabels[position], maxWidth));

		if (request.Sent.empty())
		{
			ColouredText(palette.TextDisabled, "Sent: nothing new");
		}
		else
		{
			// The biggest item is the one a reader looking at a jump in the context wants named.
			const SentItem& largest =
				*std::max_element(request.Sent.begin(), request.Sent.end(),
								  [](const SentItem& a, const SentItem& b) { return a.Bytes < b.Bytes; });

			// "—" is U+2014.
			const std::size_t items = request.Sent.size();
			const std::string prefix =
				"Sent: " + std::to_string(items) + (items == 1 ? " item" : " items") + " \xe2\x80\x94 largest: ";
			const std::string suffix = "  " + Core::FormatByteSize(largest.Bytes);

			// The label gives way to the numbers after it, down to a few characters.
			const float room =
				std::max(maxWidth - ImGui::CalcTextSize(prefix.c_str()).x - ImGui::CalcTextSize(suffix.c_str()).x,
						 ImGui::GetFontSize() * 6.0f);

			ColouredText(palette.TextDisabled, prefix + Fitted(SingleLineLabel(largest.Label, 400), room) + suffix);
		}

		ImGui::EndTooltip();
	}
}
