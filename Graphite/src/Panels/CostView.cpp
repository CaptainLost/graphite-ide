#include "Core/Base/Base.h"

#include "Panels/CostView.h"

#include "Panels/RequestRow.h"
#include "Panels/SessionDrawing.h"
#include "Sessions/SessionText.h"

#include <imgui.h>
#include <implot.h>
#include <implot_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <utility>
#include <string>
#include <vector>

namespace Graphite {

	namespace {
		CostResult Sum(const Pricing& pricing, const UsageByModel& usage)
		{
			return pricing.CostOf(usage);
		}

		void DrawTotals(const Session& session, const std::optional<SessionSummary>& summary, const Pricing& pricing)
		{
			// The session's own usage comes from the summary when there is one — it covers the
			// whole file — and from the turns read so far otherwise.
			UsageByModel main;

			if (summary.has_value())
			{
				main = summary->Main;
			}
			else
			{
				for (const Turn& turn : session.GetTurns())
					for (const auto& [key, counts] : turn.Usage)
						main[key] += counts;
			}

			const CostResult own = Sum(pricing, main);
			const CostResult subagents = summary.has_value() ? Sum(pricing, summary->Subagents) : CostResult{};

			CostResult total = own;
			total.Dollars += subagents.Dollars;
			total.UnknownModels.insert(total.UnknownModels.end(), subagents.UnknownModels.begin(),
									   subagents.UnknownModels.end());

			ImGui::Text("Total %s   session %s   subagents %s", FormatCost(total).c_str(), FormatCost(own).c_str(),
						FormatCost(subagents).c_str());

			if (!total.IsKnown())
			{
				std::string names;
				for (const std::string& model : total.UnknownModels)
					names += (names.empty() ? "" : ", ") + model;

				ImGui::TextDisabled("No price for %s. Add one under Settings > Claude.", names.c_str());
			}

			if (!ImGui::BeginTable("##models", 4, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
				return;

			ImGui::TableSetupColumn("Model");
			ImGui::TableSetupColumn("Tokens");
			ImGui::TableSetupColumn("Cost");
			ImGui::TableSetupColumn("Share");
			ImGui::TableHeadersRow();

			UsageByModel all = main;
			if (summary.has_value())
				for (const auto& [key, counts] : summary->Subagents)
					all[key] += counts;

			for (const auto& [key, counts] : all)
			{
				const CostResult cost = pricing.CostOf({{key, counts}});

				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::Text("%s%s", key.Model.c_str(), key.Fast ? " (fast)" : "");
				ImGui::TableNextColumn();
				ImGui::Text("%llu", static_cast<unsigned long long>(counts.Total()));
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(FormatCost(cost).c_str());
				ImGui::TableNextColumn();

				if (cost.IsKnown() && total.IsKnown() && total.Dollars > 0.0)
					ImGui::Text("%.0f%%", 100.0 * cost.Dollars / total.Dollars);
			}

			ImGui::EndTable();
		}

		// The chart's height, in lines of text.
		constexpr float kChartLines = 16.0f;

		// The tooltip's widest line, in font sizes, before a long heading is cut.
		constexpr float kTooltipEms = 36.0f;

		// Room left above the tallest bar and the total's end, so neither is drawn on the plot's
		// edge. ImPlot pads each end of a fit by half of this; both value axes hold their floor
		// at zero, so all of it goes on top.
		constexpr float kFitPadding = 0.12f;

		// The cost's layers, bottom up and indexed by CostLayer: the three the context is made of,
		// named and coloured as the Requests tab's context chart draws them, and the output on
		// top. They name the legend entries too.
		constexpr const char* kLayerNames[kCostLayers] = {"Cache read", "Cache write", "Uncached input", "Output"};
		constexpr const char* kTotalSeries = "Total";

		ImVec4 Colour(ImU32 colour)
		{
			return ImGui::ColorConvertU32ToFloat4(colour);
		}

		ImU32 LayerColour(const SessionPalette& palette, std::size_t layer)
		{
			switch (static_cast<CostLayer>(layer))
			{
			case CostLayer::CacheRead: return palette.Web;
			case CostLayer::CacheWrite: return palette.Edit;
			case CostLayer::Input: return palette.Run;
			case CostLayer::Output: return palette.Agent;
			}

			return palette.Other;
		}

		CostLayers LayersOf(const BucketCosts& buckets)
		{
			CostLayers layers{};
			layers[static_cast<std::size_t>(CostLayer::CacheRead)] = buckets.CacheRead;
			layers[static_cast<std::size_t>(CostLayer::CacheWrite)] = buckets.CacheWrite;
			layers[static_cast<std::size_t>(CostLayer::Input)] = buckets.Input;
			layers[static_cast<std::size_t>(CostLayer::Output)] = buckets.Output;
			return layers;
		}

		// Whether the legend shows the series: an item ImPlot has not seen yet is shown, which
		// is how it starts. Only after SetupFinish, which pushes the id the items are hashed
		// under.
		bool SeriesShown(const char* label)
		{
			const ImPlotItem* item = ImPlot::GetItem(label);
			return item == nullptr || item->Show;
		}

		std::string Dollars(double dollars)
		{
			return std::format("${:.3f}", dollars);
		}

		// The bar's buckets top down, as they are stacked, each after a swatch in its colour and
		// with the dollars lined up on the right of one column; then the bar's cost and the
		// session's up to it.
		void DrawBarTooltip(const CostBar& bar, const CostLayers& layers, double total, const SessionPalette& palette)
		{
			if (!ImGui::BeginTooltip())
				return;

			const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
			ColouredText(ink, Fitted(bar.Heading, ImGui::GetFontSize() * kTooltipEms));

			// A bar with an unpriced model shows only its priced part, so none of its numbers is
			// its cost: "?" rather than a confident, wrong figure, as the turn table says.
			const auto money = [&bar](double dollars) { return bar.Known ? Dollars(dollars) : std::string("?"); };

			std::array<std::string, kCostLayers> numbers;
			float labelWidth = 0.0f;
			float numberWidth = 0.0f;

			for (std::size_t layer = 0; layer < kCostLayers; ++layer)
			{
				numbers[layer] = money(layers[layer]);
				labelWidth = std::max(labelWidth, ImGui::CalcTextSize(kLayerNames[layer]).x);
				numberWidth = std::max(numberWidth, ImGui::CalcTextSize(numbers[layer].c_str()).x);
			}

			const float lineHeight = ImGui::GetTextLineHeight();
			const float space = ImGui::GetStyle().ItemSpacing.x;
			const float indent = ImGui::GetFontSize();
			const float swatch = std::round(lineHeight * 0.55f);

			for (std::size_t layer = kCostLayers; layer-- > 0;)
			{
				const ImVec2 start = ImGui::GetCursorScreenPos();
				const float swatchTop = std::floor(start.y + (lineHeight - swatch) * 0.5f);

				ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(start.x + indent, swatchTop),
														  ImVec2(start.x + indent + swatch, swatchTop + swatch),
														  LayerColour(palette, layer));

				ImVec2 at(start.x + indent + swatch + space, start.y);
				const float numbersRight = at.x + labelWidth + 2.0f * space + numberWidth;

				DrawPiece(at, ink, kLayerNames[layer]);

				at.x = numbersRight - ImGui::CalcTextSize(numbers[layer].c_str()).x;
				DrawPiece(at, ink, numbers[layer]);

				// The pieces go through the draw list; this is the line's one item, as wide as
				// they reach, so the tooltip is sized to them.
				ImGui::Dummy(ImVec2(numbersRight - start.x, lineHeight));
			}

			double cost = 0.0;
			for (const double dollars : layers)
				cost += dollars;

			ColouredText(ink, "Cost " + money(cost));
			ColouredText(palette.TextDisabled, "Session so far " + Dollars(total));

			ImGui::EndTooltip();
		}

		// One stacked bar per turn or request, numbered from 1, with the session's running total
		// on a second axis. Returns the index of the bar clicked, if any.
		std::optional<std::size_t> DrawChart(const char* title, const char* axisLabel,
											 const std::vector<CostLayers>& layers, const std::vector<double>& total,
											 const std::vector<CostBar>& bars, const SessionPalette& palette,
											 FollowingAxis& xAxis)
		{
			const int count = static_cast<int>(layers.size());

			if (count == 0)
			{
				ImGui::TextDisabled("Nothing to chart yet.");
				return std::nullopt;
			}

			// Kept pushed through EndPlot, which is where ImPlot reads it when it fits the axes.
			ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0.0f, kFitPadding));

			// The title's line is where DrawAxisCaptions writes too. The id behind "###" is one for
			// both scopes, so a switch keeps the legend's state.
			const std::string plotId = std::string(title) + "###costChart";

			if (!ImPlot::BeginPlot(plotId.c_str(), ImVec2(-1.0f, ImGui::GetFontSize() * kChartLines)))
			{
				ImPlot::PopStyleVar();
				return std::nullopt;
			}

			// Numbered from 1, so the bars run from 1 to count, each half a slot wide either side.
			ImPlot::SetupAxis(ImAxis_X1, axisLabel, ImPlotAxisFlags_None);
			SetupCountedAxis();
			xAxis.Setup(0.5, static_cast<double>(count) + 0.5);

			// Both value axes stand on zero: with the floor free the fit would pad below it too, and
			// the running total, fitted to its own first and last values, would make any growth
			// look like it filled the chart.
			// Named above the plot by DrawAxisCaptions, level, rather than on their side. RangeFit
			// fits them to the bars in view, so a zoomed-in stretch is not flattened by the rest.
			ImPlot::SetupAxis(ImAxis_Y1, nullptr,
							  ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_LockMin);
			ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 1.0, ImPlotCond_Once);
			ImPlot::SetupAxis(ImAxis_Y2, nullptr,
							  ImPlotAxisFlags_AuxDefault | ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit |
								  ImPlotAxisFlags_LockMin);
			ImPlot::SetupAxisLimits(ImAxis_Y2, 0.0, 1.0, ImPlotCond_Once);

			// Outside and above, so it never covers the first bars.
			ImPlot::SetupLegend(ImPlotLocation_North, ImPlotLegendFlags_Outside | ImPlotLegendFlags_Horizontal);

			// Finished by hand so the legend's show state can be read before anything is plotted.
			ImPlot::SetupFinish();

			DrawAxisCaptions("Dollars", "Total dollars");

			std::array<bool, kCostLayers> shown{};
			for (std::size_t layer = 0; layer < kCostLayers; ++layer)
				shown[layer] = SeriesShown(kLayerNames[layer]);

			std::vector<double> xs(layers.size());
			std::array<std::vector<double>, kCostLayers> tops;

			for (std::vector<double>& layerTops : tops)
				layerTops.resize(layers.size());

			for (std::size_t index = 0; index < layers.size(); ++index)
			{
				xs[index] = static_cast<double>(index + 1);
				const CostLayers stacked = StackedTops(layers[index], shown);

				for (std::size_t layer = 0; layer < kCostLayers; ++layer)
					tops[layer][index] = stacked[layer];
			}

			// Stacked by drawing each layer's bar from the top down, every one over the last, to
			// the height the layers the legend shows reach, in the scheme's colours rather than
			// the colormap's. A hidden layer is still plotted, so its legend entry stays to show
			// it again; ImPlot draws nothing for it, and the layers above it come down by what it
			// held.
			ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);

			for (std::size_t layer = kCostLayers; layer-- > 0;)
			{
				const ImVec4 colour = Colour(LayerColour(palette, layer));
				ImPlot::SetNextFillStyle(colour);
				ImPlot::SetNextLineStyle(colour);
				ImPlot::PlotBars(kLayerNames[layer], xs.data(), tops[layer].data(), count, kBarFraction);
			}

			ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
			ImPlot::SetNextLineStyle(Colour(palette.Accent));
			ImPlot::PlotLine(kTotalSeries, xs.data(), total.data(), count);

			xAxis.Track();

			// A click over a bar's column picks it, as in the Requests chart, and only without a
			// drag, which pans the chart. The legend is outside the plot, so a click on it never
			// lands here.
			std::optional<std::size_t> clicked;

			if (ImPlot::IsPlotHovered())
			{
				// Only over the bars: the chart pans past them, and a click out there is no bar's.
				const double x = ImPlot::GetPlotMousePos(ImAxis_X1, ImAxis_Y1).x;
				const double nearest = std::clamp(std::round(x), 1.0, static_cast<double>(count));
				const std::size_t index = static_cast<std::size_t>(nearest) - 1;

				if (x >= 0.5 && x <= static_cast<double>(count) + 0.5 && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
					!ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left))
					clicked = index;

				// The tooltip over the bar itself, not the gap beside it, and not while the chart
				// is being panned under the mouse.
				if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left) && std::abs(x - nearest) <= kBarFraction * 0.5 &&
					index < bars.size())
					DrawBarTooltip(bars[index], layers[index], total[index], palette);
			}

			ImPlot::EndPlot();
			ImPlot::PopStyleVar();
			return clicked;
		}

		void DrawTurnTable(const Session& session, const std::vector<TurnCost>& turns)
		{
			if (!ImGui::BeginTable("##turns", 7,
								   ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp))
				return;

			ImGui::TableSetupScrollFreeze(0, 1);
			for (const char* column : {"#", "Prompt", "Input", "Output", "Cache write", "Cache read", "Cost"})
				ImGui::TableSetupColumn(column);
			ImGui::TableHeadersRow();

			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(turns.size()));

			while (clipper.Step())
			{
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
				{
					const TurnCost& cost = turns[static_cast<std::size_t>(row)];
					const Turn& turn = session.GetTurns()[static_cast<std::size_t>(row)];

					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::Text("%d", row + 1);
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(TruncateUtf8(turn.Prompt, 80).c_str());

					const BucketCosts& buckets = cost.Buckets;

					for (const double dollars :
						 {buckets.Input, buckets.Output, buckets.CacheWrite, buckets.CacheRead, buckets.Total()})
					{
						ImGui::TableNextColumn();

						// A bucket of a turn that used an unpriced model is missing that model's
						// share, so none of its numbers is the turn's: "?" rather than a
						// confident, wrong figure.
						if (cost.Known)
							ImGui::Text("$%.3f", dollars);
						else
							ImGui::TextUnformatted("?");
					}
				}
			}

			ImGui::EndTable();
		}
	}

	std::optional<std::size_t> CostView::Draw(const Session& session, const std::optional<SessionSummary>& summary,
											  const RequestTimeline& timeline, const Pricing& pricing,
											  const SessionPalette& palette)
	{
		const std::vector<TurnCost> turns = TurnCosts(session, pricing);

		// Another reading of the transcript: where the reader had panned to named other turns.
		if (session.GetGeneration() != m_BuiltGeneration)
		{
			m_XAxis.Reset();
			m_BuiltGeneration = session.GetGeneration();
		}

		DrawTotals(session, summary, pricing);
		ImGui::Spacing();

		// In CostBy's order.
		constexpr const char* kScopes[] = {"Per turn", "Per request"};

		int by = static_cast<int>(m_By);

		// Turn 5 and request 5 are different places along the axis.
		if (SegmentedChoice(kScopes, by))
		{
			m_By = static_cast<CostBy>(by);
			m_XAxis.Reset();
		}

		std::optional<std::size_t> clicked;

		if (m_By == CostBy::Turn)
		{
			// A turn that is not Known draws its priced part, as the table below it says "?" for
			// it; the total takes nothing from it, since a number shown as a sum may not be a
			// part of one.
			std::vector<CostLayers> layers;
			std::vector<CostResult> costs;
			std::vector<CostBar> bars;
			layers.reserve(turns.size());
			costs.reserve(turns.size());
			bars.reserve(turns.size());

			for (std::size_t index = 0; index < turns.size(); ++index)
			{
				const TurnCost& turn = turns[index];
				layers.push_back(LayersOf(turn.Buckets));

				// Named by the prompt that began it, as the table below names it.
				bars.push_back({"Turn " + std::to_string(index + 1) + kDot +
									SingleLineLabel(session.GetTurns()[index].Prompt, 200),
								turn.Known});

				CostResult cost;
				cost.Dollars = turn.Buckets.Total();

				if (!turn.Known)
					cost.UnknownModels.emplace_back();

				costs.push_back(std::move(cost));
			}

			FitTo(layers.size());
			DrawChart("Cost per turn", "Turn", layers, CumulativeCost(costs), bars, palette, m_XAxis);
		}
		else
		{
			if (timeline.GetVersion() != m_BuiltVersion || pricing.GetEntries() != m_PricedWith)
				PriceRequests(timeline, pricing);

			// A click picks the request for the Requests tab to show.
			FitTo(m_RequestLayers.size());
			clicked = DrawChart("Cost per request", "Request", m_RequestLayers, m_RequestTotal, m_RequestBars, palette,
								m_XAxis);
		}

		ImGui::Spacing();
		DrawTurnTable(session, turns);
		return clicked;
	}

	void CostView::FitTo(std::size_t count)
	{
		// A new turn or request: the axis takes it in, unless the reader has moved it.
		if (count != m_ChartCount)
		{
			m_XAxis.Refit();
			m_ChartCount = count;
		}
	}

	void CostView::PriceRequests(const RequestTimeline& timeline, const Pricing& pricing)
	{
		const std::vector<ApiRequest>& requests = timeline.GetRequests();
		std::vector<CostResult> costs;
		costs.reserve(requests.size());
		m_RequestLayers.clear();
		m_RequestLayers.reserve(requests.size());
		m_RequestBars.clear();
		m_RequestBars.reserve(requests.size());

		// A request with no price has no bar, and the total runs flat across it.
		for (const ApiRequest& request : requests)
		{
			m_RequestLayers.push_back(CostLayersOf(request, pricing).value_or(CostLayers{}));
			costs.push_back(pricing.CostOf({{UsageKey{request.Model, request.Fast}, request.Usage}}));

			// Named as the Requests tab's table names it: its number, when it was answered, and
			// the model that answered.
			std::string heading = "#" + std::to_string(request.Index);

			if (const std::string time = m_Clock.Format(request.RespondedAt, ClockStyle::DateTime); !time.empty())
				heading += kDot + time;

			if (!request.Model.empty())
				heading += kDot + ShortModelName(request.Model) + (request.Fast ? " fast" : "");

			m_RequestBars.push_back({std::move(heading), costs.back().IsKnown()});
		}

		m_RequestTotal = CumulativeCost(costs);
		m_BuiltVersion = timeline.GetVersion();
		m_PricedWith = pricing.GetEntries();
	}
}
