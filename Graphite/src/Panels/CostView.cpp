#include "Panels/CostView.h"

#include "Core/Base/Base.h"

#include "Core/Base/Narrow.h"

#include "Panels/RequestRow.h"
#include "Panels/SessionDrawing.h"
#include "Panels/StackedBarChart.h"
#include "Sessions/SessionSummary.h"
#include "Sessions/SessionText.h"
#include "Sessions/ToolFacts.h"
#include "UI/SplitResults.h"

#include "Core/ImGui/EmptyState.h"

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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
				{
					for (const auto& [key, counts] : turn.Usage)
					{
						main[key] += counts;
					}
				}
			}

			const CostResult own = Sum(pricing, main);
			const CostResult subagents = summary.has_value() ? Sum(pricing, summary->Subagents) : CostResult{};

			CostResult total = own;
			total += subagents;

			// Only a session has a summary. A subagent's transcript, drawn without one, has no
			// split between itself and subagents of its own to show.
			if (summary.has_value())
			{
				ImGui::Text("Total %s   session %s   subagents %s", FormatCost(total).c_str(), FormatCost(own).c_str(),
							FormatCost(subagents).c_str());
			}
			else
			{
				ImGui::Text("Total %s", FormatCost(total).c_str());
			}

			if (!total.IsKnown())
			{
				std::string names;
				for (const std::string& model : total.UnknownModels)
				{
					names += (names.empty() ? "" : ", ") + model;
				}

				ImGui::TextDisabled("No price for %s. Add one under Settings > Claude.", names.c_str());
			}

			if (!ImGui::BeginTable("##models", 4, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
			{
				return;
			}

			ImGui::TableSetupColumn("Model");
			ImGui::TableSetupColumn("Tokens");
			ImGui::TableSetupColumn("Cost");
			ImGui::TableSetupColumn("Share");
			ImGui::TableHeadersRow();

			UsageByModel all = main;
			if (summary.has_value())
			{
				for (const auto& [key, counts] : summary->Subagents)
				{
					all[key] += counts;
				}
			}

			for (const auto& [key, counts] : all)
			{
				const CostResult cost = pricing.CostOf(key, counts);

				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				// Named as the Requests tab names a model; the full id is what Settings > Claude
				// prices it by, so it stays a hover away.
				const std::string model = ModelLabel(key.Model, key.Fast);
				ImGui::TextUnformatted(model.c_str());
				ImGui::SetItemTooltip("%s", key.Model.c_str());
				ImGui::TableNextColumn();
				// Shortened as the Requests tab writes tokens: "51.2M", not eight digits to count.
				ImGui::TextUnformatted(FormatTokens(counts.Total()).c_str());
				ImGui::SetItemTooltip("%s tokens", FormatTokensExact(counts.Total()).c_str());
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(FormatCost(cost).c_str());
				ImGui::TableNextColumn();

				if (cost.IsKnown() && total.IsKnown() && total.Dollars > 0.0)
				{
					ImGui::Text("%.0f%%", 100.0 * cost.Dollars / total.Dollars);
				}
			}

			ImGui::EndTable();
		}

		// The most of a prompt a tooltip shows. A pasted log can run to tens of thousands of
		// bytes, which no tooltip has room for.
		constexpr std::size_t kTooltipPromptBytes = 2000;

		// The most of a prompt the turn's pane shows, where it scrolls: more than a tooltip has
		// room for, short of a pasted log that is wrapped again every frame.
		constexpr std::size_t kPanePromptBytes = 20000;

		// A prompt's row in the table: made one line, as the chart's heading makes it.
		constexpr std::size_t kRowPromptBytes = 1000;

		// The prompt as a tooltip shows it: its own lines kept, wrapped to the tooltip's width,
		// and cut with an ellipsis past kTooltipPromptBytes.
		void WrappedPrompt(const std::string& prompt)
		{
			std::string shown = TruncateUtf8(prompt, kTooltipPromptBytes);
			if (shown.size() < prompt.size())
			{
				shown += kEllipsis;
			}

			ImGui::PushTextWrapPos(ImGui::GetFontSize() * kTooltipEms);
			ImGui::TextUnformatted(shown.c_str(), shown.c_str() + shown.size());
			ImGui::PopTextWrapPos();
		}

		// The cost's layers, bottom up and indexed by CostLayer: the three the context is made of,
		// named and coloured as the Requests tab's context chart draws them, and the output on
		// top. They name the legend entries too.
		constexpr const char* kLayerNames[kCostLayers] = {"Cache read", "Cache write", "Uncached input", "Output"};
		constexpr const char* kTotalSeries = "Total";

		ImU32 LayerColour(const SessionPalette& palette, std::size_t layer)
		{
			switch (static_cast<CostLayer>(layer))
			{
			case CostLayer::CacheRead:
				return palette.Web;
			case CostLayer::CacheWrite:
				return palette.Edit;
			case CostLayer::Input:
				return palette.Run;
			case CostLayer::Output:
				return palette.Agent;
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

		// Written as the Requests tab writes a cost, in its table and its chart's tooltip, so the
		// same request reads the same in both tabs.
		std::string Dollars(double dollars)
		{
			CostResult cost;
			cost.Dollars = dollars;
			return FormatCost(cost);
		}

		// Laid out as the Requests chart's tooltip is: the heading, the bar's cost, its buckets in
		// the order the legend's layers stack from the bottom, each after a swatch in its colour
		// with the dollars lined up on the right of one column, and last the session's cost up
		// to it.
		void DrawBarTooltip(const CostBar& bar, const CostLayers& layers, double total, const SessionPalette& palette)
		{
			if (!ImGui::BeginTooltip())
			{
				return;
			}

			const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);

			// Wrapped rather than cut: a turn's heading is its prompt, and the prompt is what
			// says which turn the bar is.
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * kTooltipEms);
			ColouredText(ink, bar.Heading);
			ImGui::PopTextWrapPos();

			// A bar with an unpriced model shows only its priced part, so none of its numbers is
			// its cost: "?" rather than a confident, wrong figure, as the turn table says.
			const auto money = [&bar](double dollars) { return bar.Known ? Dollars(dollars) : std::string("?"); };

			double cost = 0.0;
			for (const double dollars : layers)
			{
				cost += dollars;
			}

			ColouredText(ink, "Cost " + money(cost));

			std::array<SwatchRow, kCostLayers> rows;
			for (std::size_t layer = 0; layer < kCostLayers; ++layer)
			{
				rows[layer] = {.Colour = LayerColour(palette, layer), .Label = kLayerNames[layer], .Number = money(layers[layer])};
			}

			DrawSwatchRows(rows, ink);

			ColouredText(palette.TextDisabled, "Session so far " + Dollars(total));

			ImGui::EndTooltip();
		}

		// One stacked bar per turn or request, numbered from 1, with the session's running total
		// on a second axis, and the bar at index `selected` banded. Returns the index of the bar
		// clicked, if any.
		std::optional<std::size_t> DrawChart(const char* title, const char* axisLabel,
											 const std::vector<CostLayers>& layers, const std::vector<double>& total,
											 const std::vector<CostBar>& bars, std::optional<std::size_t> selected,
											 const SessionPalette& palette, FollowingAxis& xAxis)
		{
			const int count = Core::Narrow<int>(layers.size());

			if (count == 0)
			{
				// The chart's own height, so what follows starts where it would under a chart.
				Core::EmptyState("Nothing to chart yet.", nullptr, ImVec2(0.0f, ImGui::GetFontSize() * kChartLines));
				return std::nullopt;
			}

			// One id for both scopes, so a switch keeps the legend's state.
			if (!BeginBarChart(title, "costChart"))
			{
				return std::nullopt;
			}

			// Numbered from 1, so the bars run from 1 to count, each half a slot wide either side.
			ImPlot::SetupAxis(ImAxis_X1, axisLabel, ImPlotAxisFlags_None);
			SetupCountedAxis();
			xAxis.Setup(0.5, static_cast<double>(count) + 0.5);

			// Both value axes stand on zero: the running total, fitted to its own first and last
			// values, would make any growth look like it filled the chart.
			FinishBarChartSetup({.BarCaption = "Dollars", .LineCaption = "Total dollars", .LineFromZero = true});

			// Under the bars, as the Requests chart bands its selection: a bar is one slot wide.
			if (selected && *selected < layers.size())
			{
				DrawColumnBand(static_cast<double>(*selected + 1), 1.0, palette.Accent);
			}

			std::array<bool, kCostLayers> shown{};
			for (std::size_t layer = 0; layer < kCostLayers; ++layer)
			{
				shown[layer] = SeriesShown(kLayerNames[layer]);
			}

			std::vector<double> xs(layers.size());
			std::array<std::vector<double>, kCostLayers> tops;

			for (std::vector<double>& layerTops : tops)
			{
				layerTops.resize(layers.size());
			}

			for (std::size_t index = 0; index < layers.size(); ++index)
			{
				xs[index] = static_cast<double>(index + 1);
				const CostLayers stacked = StackedTops(layers[index], shown);

				for (std::size_t layer = 0; layer < kCostLayers; ++layer)
				{
					tops[layer][index] = stacked[layer];
				}
			}

			std::array<BarLayer, kCostLayers> barLayers{};
			for (std::size_t layer = 0; layer < kCostLayers; ++layer)
			{
				barLayers[layer] = {kLayerNames[layer], PlotColour(LayerColour(palette, layer)), tops[layer]};
			}

			PlotStackedBars(barLayers, xs, kBarFraction);

			ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
			ImPlot::SetNextLineStyle(PlotColour(palette.Accent));
			ImPlot::PlotLine(kTotalSeries, xs.data(), total.data(), count);

			xAxis.Track();

			// A click over a bar's column picks it, as in the Requests chart. The legend is outside
			// the plot, so a click on it never lands here.
			std::optional<std::size_t> clicked;

			if (ImPlot::IsPlotHovered())
			{
				// Only over the bars: the chart pans past them, and a click out there is no bar's.
				const double x = ImPlot::GetPlotMousePos(ImAxis_X1, ImAxis_Y1).x;
				const double nearest = std::clamp(std::round(x), 1.0, static_cast<double>(count));
				const std::size_t index = static_cast<std::size_t>(nearest) - 1;

				if (x >= 0.5 && x <= static_cast<double>(count) + 0.5 && ColumnClicked())
				{
					clicked = index;
				}

				// The tooltip over the bar itself, not the gap beside it, and not while the chart
				// is being panned under the mouse.
				if (!ChartPanning() && std::abs(x - nearest) <= kBarFraction * 0.5 && index < bars.size())
				{
					DrawBarTooltip(bars[index], layers[index], total[index], palette);
				}
			}

			EndBarChart();
			return clicked;
		}

		// A cell's worth under a cent reads as nothing spent, and is written so.
		constexpr double kNegligibleDollars = 0.01;

		float SwatchSize()
		{
			return std::round(ImGui::GetTextLineHeight() * 0.55f);
		}

		// A square in `colour`, vertically centred on the line the cursor stands on, and the
		// cursor moved past it.
		void Swatch(ImU32 colour)
		{
			const ImVec2 at = ImGui::GetCursorScreenPos();
			const float swatch = SwatchSize();
			const float top = std::floor(at.y + (ImGui::GetTextLineHeight() - swatch) * 0.5f);
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(at.x, top), ImVec2(at.x + swatch, top + swatch), colour);
			ImGui::Dummy(ImVec2(swatch, ImGui::GetTextLineHeight()));
			ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
		}

		// `text` against the right edge of the cell, so a column of numbers lines up by its
		// last digit, as numbers are compared.
		void RightAligned(ImU32 colour, const std::string& text)
		{
			const float width = ImGui::CalcTextSize(text.c_str()).x;
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - width));
			ColouredText(colour, text);
		}

		// A header over a column of numbers, against the right edge as they are. TableHeader
		// writes its label where the cursor stands and still takes the whole cell as its own, so
		// the cursor is moved to where the label must end at the cell's edge.
		void RightHeaderInCell(const char* name)
		{
			const float width = ImGui::CalcTextSize(name).x;
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - width));
			ImGui::TableHeader(name);
		}

		// Padding around a roomy table's cells: a little more than ImGui's default.
		ImVec2 RoomyCellPadding()
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			return ImVec2(style.CellPadding.x + ImGui::GetFontSize() * 0.25f, style.CellPadding.y + 2.0f);
		}

		// A cost in a cell. A turn that used an unpriced model is missing that model's share, so
		// its number is not the turn's: "?" rather than a confident, wrong figure.
		void MoneyCell(double dollars, bool known, const SessionPalette& palette)
		{
			ImGui::TableNextColumn();

			if (!known)
			{
				RightAligned(palette.TextDisabled, "?");
				return;
			}

			const ImU32 ink = dollars < kNegligibleDollars ? palette.TextDisabled : ImGui::GetColorU32(ImGuiCol_Text);
			RightAligned(ink, Dollars(dollars));
		}

		// Striped rather than ruled, and a little more room around each cell than ImGui's
		// default, so a row reads across the gap between the prompt and its numbers. Returns
		// whether the table is open, to be ended by EndRoomyTable as BeginTable's is by EndTable.
		// The padding stays pushed until then: BeginTable takes its width once, but every
		// TableNextRow takes its height from the style again.
		bool BeginRoomyTable(const char* id, int columns, ImGuiTableFlags flags, ImVec2 size = ImVec2(0.0f, 0.0f))
		{
			ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, RoomyCellPadding());
			const bool open = ImGui::BeginTable(id, columns, ImGuiTableFlags_RowBg | flags, size);

			if (!open)
			{
				ImGui::PopStyleVar();
			}

			return open;
		}

		void EndRoomyTable()
		{
			ImGui::EndTable();
			ImGui::PopStyleVar();
		}

		// The width a cost's column needs for the widest it writes.
		float CostWidth()
		{
			return std::max(ImGui::CalcTextSize("< $0.01").x, ImGui::CalcTextSize("$000.00").x);
		}

		// A bar as tall as a swatch and centred on the line as one is, from `left` to `right`.
		// Only the draw list writes it, so the cell takes its height from the row's other items.
		void LineBar(float left, float right, ImU32 colour)
		{
			const float height = SwatchSize();
			const float top = std::floor(ImGui::GetCursorScreenPos().y + (ImGui::GetTextLineHeight() - height) * 0.5f);
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(left, top), ImVec2(right, top + height), colour);
		}

		// The turn's cost as one bar across the cell, as long against the cell as the turn is
		// against `scale`, made of its layers in the chart's order and colours. Each layer's ends
		// are rounded to the pixel from the running sum, so the pieces meet without a gap.
		void StackedBar(const CostLayers& layers, double scale, const SessionPalette& palette)
		{
			if (scale <= 0.0)
			{
				return;
			}

			const float left = ImGui::GetCursorScreenPos().x;
			const double room = ImGui::GetContentRegionAvail().x;
			double sum = 0.0;

			for (std::size_t layer = 0; layer < kCostLayers; ++layer)
			{
				const float start = left + static_cast<float>(std::round(room * sum / scale));
				sum += layers[layer];
				const float end = left + static_cast<float>(std::round(room * sum / scale));

				if (end > start)
				{
					LineBar(start, end, LayerColour(palette, layer));
				}
			}
		}

		// Where a column's contents start and end on screen, taken from the header row, which
		// is drawn every frame whatever has scrolled under it.
		struct ColumnEdges
		{
			float Left = 0.0f;
			float Right = 0.0f;
		};

		ColumnEdges NextColumnEdges()
		{
			ImGui::TableNextColumn();
			const float left = ImGui::GetCursorScreenPos().x;
			return {left, left + ImGui::GetContentRegionAvail().x};
		}

		// The height of one of a roomy table's rows.
		float RoomyRowHeight()
		{
			return ImGui::GetTextLineHeight() + RoomyCellPadding().y * 2.0f;
		}

		// The turns' sum, under the table rather than in it, so it stays in sight however far
		// the turns are scrolled. Set apart as the header is, and ruled off from the rows above.
		// Its numbers stand under the columns they sum, by the edges the header row found.
		void DrawTurnFooter(const ColumnEdges& prompt, const ColumnEdges& requestsColumn,
							const ColumnEdges& costColumn, std::size_t requests, double dollars, bool known,
							const SessionPalette& palette)
		{
			// Against the table's bottom edge, as its last row would be, rather than an item's
			// spacing below it.
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y);

			const ImVec2 at = ImGui::GetCursorScreenPos();
			const float width = ImGui::GetContentRegionAvail().x;
			const float height = RoomyRowHeight();
			ImDrawList* drawList = ImGui::GetWindowDrawList();

			drawList->AddRectFilled(at, ImVec2(at.x + width, at.y + height), ImGui::GetColorU32(ImGuiCol_TableHeaderBg));
			drawList->AddLine(at, ImVec2(at.x + width, at.y), ImGui::GetColorU32(ImGuiCol_TableBorderStrong));

			const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
			const float y = at.y + (height - ImGui::GetTextLineHeight()) * 0.5f;

			ImVec2 piece(prompt.Left, y);
			DrawPiece(piece, ink, "Total");

			const std::string count = std::to_string(requests);
			piece = ImVec2(requestsColumn.Right - ImGui::CalcTextSize(count.c_str()).x, y);
			DrawPiece(piece, ink, count);

			const std::string cost = known ? Dollars(dollars) : std::string{"?"};
			piece = ImVec2(costColumn.Right - ImGui::CalcTextSize(cost.c_str()).x, y);
			DrawPiece(piece, known ? ink : palette.TextDisabled, cost);

			// The pieces go through the draw list; this is the line's one item.
			ImGui::Dummy(ImVec2(width, height));
		}

		// The turns, one row each, with each turn's cost drawn as a bar against the dearest's,
		// and their sum pinned under them. Returns the turn whose row was clicked.
		std::optional<std::size_t> DrawTurnTable(const Session& session, const std::vector<TurnCost>& turns,
												 std::optional<std::size_t> selected, const SessionPalette& palette)
		{
			const bool footer = turns.size() > 1;

			// No taller than its rows and header, so a few turns keep the sum right under them;
			// many scroll, above the sum at the bottom. The pixels past the rows are the outer
			// borders', and keep a table that just fits from growing a scrollbar.
			float height = 0.0f;
			if (footer)
			{
				const float rows = RoomyRowHeight() * static_cast<float>(turns.size() + 1) + 2.0f;
				height = std::max(std::min(rows, ImGui::GetContentRegionAvail().y - RoomyRowHeight()),
								  RoomyRowHeight() * 2.0f);
			}

			if (!BeginRoomyTable("##turns", 5,
								 ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_ScrollY | ImGuiTableFlags_PadOuterX,
								 ImVec2(0.0f, height)))
			{
				return std::nullopt;
			}

			ImGui::TableSetupScrollFreeze(0, 1);

			// The prompt takes whatever the numbers leave, and is cut to it. The numbers' columns
			// are as wide as their widest: a table that cannot be resized gives a fixed column the
			// width it is set up with every frame, so they follow the font. Were the prompt's width
			// taken from its text, as the other sizing policies take it, a prompt cut to the column
			// would narrow the column.
			ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Prompt", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Requests", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Requests").x);
			ImGui::TableSetupColumn("##bar", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 8.0f);
			ImGui::TableSetupColumn("Cost", ImGuiTableColumnFlags_WidthFixed, CostWidth());

			// Laid out by hand, rather than by TableHeadersRow, so the numbers' headers stand over
			// their right edge.
			ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
			NextColumnEdges();
			ImGui::TableHeader("#");
			const ColumnEdges promptEdges = NextColumnEdges();
			ImGui::TableHeader("Prompt");
			const ColumnEdges requestsEdges = NextColumnEdges();
			RightHeaderInCell("Requests");
			NextColumnEdges();
			ImGui::TableHeader("##bar");
			const ColumnEdges costEdges = NextColumnEdges();
			RightHeaderInCell("Cost");

			// The bars are measured against the dearest turn, so it fills its cell.
			double dearest = 0.0;
			for (const TurnCost& cost : turns)
			{
				dearest = std::max(dearest, cost.Buckets.Total());
			}

			const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
			std::optional<std::size_t> clicked;

			ImGuiListClipper clipper;
			clipper.Begin(Core::Narrow<int>(turns.size()));

			while (clipper.Step())
			{
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
				{
					const std::size_t index = static_cast<std::size_t>(row);
					const TurnCost& cost = turns[index];
					const Turn& turn = session.GetTurns()[index];

					ImGui::TableNextRow();
					ImGui::TableNextColumn();

					// The row's one item that takes a click, across every column, as the Requests
					// tab's rows are. The open turn's row is washed in the accent, so it is told
					// apart from the row under the mouse, which the theme's header colour marks.
					ImGui::PushID(row);
					ImVec4 wash = ImGui::ColorConvertU32ToFloat4(palette.Accent);
					wash.w = 0.35f;
					ImGui::PushStyleColor(ImGuiCol_Header, wash);
					if (ImGui::Selectable("##row", selected == index,
										  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
					{
						clicked = index;
					}
					ImGui::PopStyleColor();
					ImGui::PopID();

					if (!turn.Prompt.empty() && ImGui::BeginItemTooltip())
					{
						WrappedPrompt(turn.Prompt);
						ImGui::EndTooltip();
					}

					ImGui::SameLine(0.0f, 0.0f);
					RightAligned(palette.TextDisabled, std::to_string(row + 1));

					ImGui::TableNextColumn();

					if (turn.Prompt.empty())
					{
						ColouredText(palette.TextDisabled, "(no prompt)");
					}
					else
					{
						ColouredText(ink, Fitted(SingleLineLabel(turn.Prompt, kRowPromptBytes),
												 ImGui::GetContentRegionAvail().x));
					}

					// A turn that sent nothing, as the one before the first prompt, is quiet all
					// the way across, as its prompt and cost already are.
					ImGui::TableNextColumn();
					RightAligned(turn.Requests.empty() ? palette.TextDisabled : ink,
								 std::to_string(turn.Requests.size()));

					ImGui::TableNextColumn();
					StackedBar(LayersOf(cost.Buckets), dearest, palette);

					MoneyCell(cost.Buckets.Total(), cost.Known, palette);
				}
			}

			EndRoomyTable();

			// The session's own turns summed. Only a known sum is written: one with an unpriced
			// part in it would read as the whole.
			if (footer)
			{
				bool known = true;
				double dollars = 0.0;
				std::size_t requests = 0;

				for (std::size_t index = 0; index < turns.size(); ++index)
				{
					known = known && turns[index].Known;
					dollars += turns[index].Buckets.Total();
					requests += session.GetTurns()[index].Requests.size();
				}

				DrawTurnFooter(promptEdges, requestsEdges, costEdges, requests, dollars, known, palette);
			}

			return clicked;
		}

		// Where the turn's money went, bucket by bucket, the dearest first: its swatch and name as
		// the chart's legend has them, the dollars, the share of the turn, and a bar as long.
		void DrawBuckets(const TurnCost& cost, const SessionPalette& palette)
		{
			const CostLayers layers = LayersOf(cost.Buckets);
			const double total = cost.Buckets.Total();

			std::array<std::size_t, kCostLayers> order{};
			for (std::size_t layer = 0; layer < kCostLayers; ++layer)
			{
				order[layer] = layer;
			}
			std::ranges::stable_sort(order, [&layers](std::size_t a, std::size_t b) { return layers[a] > layers[b]; });

			if (!ImGui::BeginTable("##buckets", 4, ImGuiTableFlags_None))
			{
				return;
			}

			float nameWidth = 0.0f;
			for (const char* name : kLayerNames)
			{
				nameWidth = std::max(nameWidth, ImGui::CalcTextSize(name).x);
			}

			ImGui::TableSetupColumn("Bucket", ImGuiTableColumnFlags_WidthFixed,
									SwatchSize() + ImGui::GetStyle().ItemInnerSpacing.x + nameWidth);
			ImGui::TableSetupColumn("Dollars", ImGuiTableColumnFlags_WidthFixed, CostWidth());
			ImGui::TableSetupColumn("Share", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("100%").x);
			ImGui::TableSetupColumn("Bar", ImGuiTableColumnFlags_WidthStretch);

			const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);

			for (const std::size_t layer : order)
			{
				const double share = total > 0.0 ? layers[layer] / total : 0.0;
				const ImU32 text = cost.Known && layers[layer] >= kNegligibleDollars ? ink : palette.TextDisabled;

				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				Swatch(LayerColour(palette, layer));
				ColouredText(text, kLayerNames[layer]);

				MoneyCell(layers[layer], cost.Known, palette);

				ImGui::TableNextColumn();
				if (cost.Known)
				{
					RightAligned(text, std::format("{:.0f}%", 100.0 * share).c_str());
				}

				// No bar for a share under a pixel: a sliver reads as a mark, not as nothing.
				ImGui::TableNextColumn();
				if (cost.Known)
				{
					const float left = ImGui::GetCursorScreenPos().x;
					const float width = std::round(ImGui::GetContentRegionAvail().x * static_cast<float>(share));

					if (width >= 1.0f)
					{
						LineBar(left, left + width, LayerColour(palette, layer));
					}
				}
			}

			ImGui::EndTable();
		}

		// The tools the turn called, a row each, laid out as the buckets above them are: the
		// tool's icon in its category's colour and its name, how many times, and a bar as long
		// against the most called one's.
		void DrawTools(const std::vector<ToolCount>& tools, const SessionPalette& palette)
		{
			if (tools.empty())
			{
				ColouredText(palette.TextDisabled, "No tools called.");
				return;
			}

			if (!ImGui::BeginTable("##tools", 3, ImGuiTableFlags_None))
			{
				return;
			}

			std::size_t most = 0;
			float nameWidth = 0.0f;
			std::vector<std::string> names;
			names.reserve(tools.size());

			for (const ToolCount& tool : tools)
			{
				most = std::max(most, tool.Count);
				names.push_back(SingleLineLabel(tool.Name, 80));
				nameWidth = std::max(nameWidth, ImGui::CalcTextSize(names.back().c_str()).x);
			}

			// The name's column no wider than the buckets' leaves it, so the counts and bars keep
			// room beside a long MCP tool's name, which is cut to it.
			const float iconWidth = ImGui::CalcTextSize(ICON_MDI_CONSOLE).x + ImGui::GetStyle().ItemInnerSpacing.x;
			ImGui::TableSetupColumn("Tool", ImGuiTableColumnFlags_WidthFixed,
									std::min(iconWidth + nameWidth, ImGui::GetFontSize() * 12.0f));
			ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("\xc3\x97"
																								   "000")
																				   .x);
			ImGui::TableSetupColumn("Bar", ImGuiTableColumnFlags_WidthStretch);

			const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);

			for (std::size_t index = 0; index < tools.size(); ++index)
			{
				const ToolCount& tool = tools[index];
				const ToolCategory category = CategoryOf(tool.Name);
				const ImU32 colour = palette.CategoryColor(category);

				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ColouredText(colour, IconOf(category));
				ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
				ColouredText(ink, Fitted(names[index], ImGui::GetContentRegionAvail().x));

				ImGui::TableNextColumn();
				RightAligned(ink, "\xc3\x97" + std::to_string(tool.Count));

				ImGui::TableNextColumn();
				const float left = ImGui::GetCursorScreenPos().x;
				const float width = std::round(ImGui::GetContentRegionAvail().x * static_cast<float>(tool.Count) /
											   static_cast<float>(most));

				// Quiet rather than in the category's colour, which the buckets above use for
				// their layers: a green tool's bar would read as money spent on uncached input.
				if (width >= 1.0f)
				{
					LineBar(left, left + width, palette.TextDisabled);
				}
			}

			ImGui::EndTable();
		}

		// "Read D:\Repos\IDE\a.cpp" as "Read a.cpp": in a row a third of the pane wide, the
		// path's start is all that would fit, and it is the same for every file. Only a line
		// that is a name and one path; anything with a space after the name is left whole.
		std::string Compacted(const std::string& line)
		{
			const std::size_t space = line.find(' ');
			if (space == std::string::npos)
			{
				return line;
			}

			const std::string_view rest = std::string_view{line}.substr(space + 1);
			const std::size_t slash = rest.find_last_of("/\\");

			if (rest.find(' ') != std::string_view::npos || slash == std::string_view::npos || slash + 1 == rest.size())
			{
				return line;
			}

			return line.substr(0, space + 1) + std::string{rest.substr(slash + 1)};
		}

		// What a request did, as short as a row can hold it: each tool by its name alone, but a
		// file tool by its file, since which file is the point; repeats counted rather than
		// written again ("Bash ×3"); a reply as the start of it, in its quotes. The commands and
		// inputs are the tooltip's.
		std::string ShortActivity(const std::vector<std::string>& activity)
		{
			std::vector<std::pair<std::string, std::size_t>> counted;

			for (const std::string& line : activity)
			{
				std::string brief = Compacted(line);

				// Neither a file nor a reply: the tool's name, before its input.
				if (brief == line && !line.starts_with("\xe2\x80\x9c"))
				{
					brief = line.substr(0, line.find(' '));
				}

				const auto seen = std::ranges::find(counted, brief, &std::pair<std::string, std::size_t>::first);
				if (seen == counted.end())
				{
					counted.emplace_back(std::move(brief), 1);
				}
				else
				{
					++seen->second;
				}
			}

			std::string text;
			for (const auto& [brief, count] : counted)
			{
				text += (text.empty() ? "" : ", ") + brief + (count > 1 ? " \xc3\x97" + std::to_string(count) : "");
			}

			return text;
		}

		// A section of the turn's pane: its title, then a child as tall as the pane has left, so
		// each section scrolls on its own and the pane never does. EndChild closes it, whatever
		// this returns.
		bool BeginSection(const char* title, const char* id)
		{
			ImGui::SeparatorText(title);

			// Short of the cell's padding below it, which would otherwise take the pane a pixel
			// or two past its bottom and give it a scrollbar of its own.
			const float height = std::max(ImGui::GetContentRegionAvail().y - ImGui::GetStyle().CellPadding.y * 2.0f,
										  ImGui::GetTextLineHeightWithSpacing() * 3.0f);
			return ImGui::BeginChild(id, ImVec2(0.0f, height));
		}

		// The most lines of a prompt the pane shows before it scrolls the rest.
		constexpr float kPromptBoxLines = 3.0f;

		// The turn's prompt across the pane's whole width, framed as a quote: as tall as its
		// wrapped text up to kPromptBoxLines, and scrolled past that, so a short prompt takes no
		// more than its line and a pasted log does not push the numbers out of sight.
		void DrawPromptBox(const std::string& text, const SessionPalette& palette)
		{
			std::string prompt = TruncateUtf8(text, kPanePromptBytes);
			if (prompt.size() < text.size())
			{
				prompt += kEllipsis;
			}

			const ImGuiStyle& style = ImGui::GetStyle();
			const float wrap = ImGui::GetContentRegionAvail().x - style.FramePadding.x * 2.0f;
			const float textHeight =
				prompt.empty() ? ImGui::GetTextLineHeight()
							   : ImGui::CalcTextSize(prompt.c_str(), prompt.c_str() + prompt.size(), false, wrap).y;
			const float height =
				std::min(textHeight, ImGui::GetTextLineHeight() * kPromptBoxLines) + style.FramePadding.y * 2.0f;

			if (ImGui::BeginChild("##turnPrompt", ImVec2(0.0f, height), ImGuiChildFlags_FrameStyle))
			{
				if (prompt.empty())
				{
					ColouredText(palette.TextDisabled, "(no prompt)");
				}
				else
				{
					ImGui::PushTextWrapPos(0.0f);
					ImGui::TextUnformatted(prompt.c_str(), prompt.c_str() + prompt.size());
					ImGui::PopTextWrapPos();
				}
			}

			ImGui::EndChild();
		}
	}

	std::optional<std::size_t> CostView::DrawTurnDetails(const Session& session, const std::vector<TurnCost>& turns,
														 const RequestTimeline& timeline, const Pricing& pricing,
														 const SessionPalette& palette)
	{
		if (!m_SelectedTurn || *m_SelectedTurn >= turns.size())
		{
			return std::nullopt;
		}

		const std::size_t index = *m_SelectedTurn;
		const TurnCost& cost = turns[index];
		const Turn& turn = session.GetTurns()[index];
		const ImGuiStyle& style = ImGui::GetStyle();
		const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);

		// Headed as the Requests tab's pane is, the close last on the line in its corner and the
		// line cut short of it.
		const std::string heading = "Turn " + std::to_string(index + 1) + kDot +
									(cost.Known ? Dollars(cost.Buckets.Total()) : std::string{"?"}) + kDot +
									std::to_string(turn.Requests.size()) +
									(turn.Requests.size() == 1 ? " request" : " requests");
		const float closeWidth = ImGui::CalcTextSize(ICON_MDI_CLOSE).x + style.FramePadding.x * 2.0f;
		ColouredText(ink, Fitted(heading, std::max(ImGui::GetContentRegionAvail().x - closeWidth - style.ItemSpacing.x,
												   0.0f)));

		if (DrawPaneClose())
		{
			m_SelectedTurn.reset();
			return std::nullopt;
		}

		ImGui::Spacing();

		const TurnBreakdown breakdown = BreakDownTurn(session, index, timeline, pricing);
		std::optional<std::size_t> clicked;

		DrawPromptBox(turn.Prompt, palette);
		ImGui::Spacing();

		// Two columns under the prompt, the requests given the more room: their rows carry what
		// each request did, where the money's are a few short lines.
		if (!ImGui::BeginTable("##turnDetailColumns", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX))
		{
			return std::nullopt;
		}

		ImGui::TableSetupColumn("Money", ImGuiTableColumnFlags_WidthStretch, 2.0f);
		ImGui::TableSetupColumn("Requests", ImGuiTableColumnFlags_WidthStretch, 3.0f);

		ImGui::TableNextRow();
		ImGui::TableNextColumn();

		if (BeginSection("Where it went", "##turnMoney"))
		{
			DrawBuckets(cost, palette);
			ImGui::Spacing();
			ImGui::SeparatorText("Tools");
			DrawTools(breakdown.Tools, palette);
		}

		ImGui::EndChild();

		ImGui::TableNextColumn();

		if (BeginSection("Requests", "##turnRequestList"))
		{
			const std::vector<ApiRequest>& requests = timeline.GetRequests();

			// Each with the timeline's request behind it, where the timeline has read it. No cost
			// among the columns: a size is what the transcript says, where a cost is only as right
			// as the price list, and the tooltip has it.
			std::vector<std::pair<const TurnRequest*, const ApiRequest*>> rows;
			rows.reserve(breakdown.Requests.size());

			for (const TurnRequest& request : breakdown.Requests)
			{
				rows.emplace_back(&request, request.Position && *request.Position < requests.size()
												? &requests[*request.Position]
												: nullptr);
			}

			// In the order they were sent until the reader sorts by a header; the header stays
			// over the rows as they scroll under it.
			if (rows.empty())
			{
				ColouredText(palette.TextDisabled, "No requests.");
			}
			else if (BeginRoomyTable("##turnRequests", 4,
									 ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersOuterH))
			{
				// Room beside each sortable header for its arrow.
				const float arrow = ImGui::GetFontSize();

				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort,
										ImGui::CalcTextSize("#0000").x + arrow);
				ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,
										ImGui::CalcTextSize("00:00:00").x);
				ImGui::TableSetupColumn("Context",
										ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending,
										std::max(ImGui::CalcTextSize("000.0k").x, ImGui::CalcTextSize("Context").x) + arrow);
				ImGui::TableSetupColumn("Response", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort);
				ImGui::TableHeadersRow();

				// Sorted again every frame: a turn's requests are few enough, and a live turn gains
				// them. One the timeline has not read has neither a number nor a size, and comes
				// last either way.
				const ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs();
				const bool byContext = specs != nullptr && specs->SpecsCount > 0 && specs->Specs[0].ColumnIndex == 2;
				const bool descending =
					specs != nullptr && specs->SpecsCount > 0 &&
					specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;

				std::ranges::stable_sort(rows, [byContext, descending](const auto& a, const auto& b) {
					if ((a.second != nullptr) != (b.second != nullptr))
					{
						return a.second != nullptr;
					}
					if (a.second == nullptr)
					{
						return false;
					}

					const std::uint64_t left = byContext ? a.second->Context : a.second->Index;
					const std::uint64_t right = byContext ? b.second->Context : b.second->Index;
					return descending ? left > right : left < right;
				});

				// A turn can run to hundreds of requests; only the rows in sight are laid out.
				ImGuiListClipper clipper;
				clipper.Begin(Core::Narrow<int>(rows.size()));

				while (clipper.Step())
				{
					for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
					{
						const auto [request, found] = rows[static_cast<std::size_t>(row)];
						const std::vector<std::string> activity =
							found != nullptr ? RequestActivity(*found) : std::vector<std::string>{};

						ImGui::TableNextRow();
						ImGui::TableNextColumn();

						// A click shows the request in the Requests tab, as a bar of the per-request
						// chart does. One the timeline has not read has nowhere to go, and takes no
						// click.
						ImGui::PushID(row);
						ImGui::BeginDisabled(found == nullptr);
						if (ImGui::Selectable("##request", false,
											  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
						{
							clicked = request->Position;
						}
						ImGui::EndDisabled();
						ImGui::PopID();

						// The rest of it, which the row leaves out: the request's line as the
						// Requests tab writes it, its cost among the rest, and everything it did in
						// full, a line each.
						if (found != nullptr && ImGui::BeginItemTooltip())
						{
							ImGui::TextUnformatted(RequestLine(*found, pricing, m_Clock, ClockStyle::DateTime).c_str());

							ImGui::PushTextWrapPos(ImGui::GetFontSize() * kTooltipEms);
							for (const std::string& line : activity)
							{
								ColouredText(palette.TextDisabled, line);
							}
							ImGui::PopTextWrapPos();

							ImGui::Spacing();
							ColouredText(palette.TextDisabled, "Click to show in the Requests tab");
							ImGui::EndTooltip();
						}

						// Under its header's "#", on the left, as the sort arrow takes the cell's
						// right end.
						ImGui::SameLine(0.0f, 0.0f);
						ColouredText(palette.TextDisabled, found ? std::to_string(found->Index) : std::string{"?"});

						ImGui::TableNextColumn();
						if (found != nullptr)
						{
							ColouredText(palette.TextDisabled, m_Clock.Format(found->RespondedAt, ClockStyle::Time));
						}

						ImGui::TableNextColumn();
						if (found != nullptr)
						{
							RightAligned(ink, FormatTokens(found->Context));
						}

						ImGui::TableNextColumn();
						ColouredText(ink, Fitted(ShortActivity(activity), ImGui::GetContentRegionAvail().x));
					}
				}

				EndRoomyTable();
			}
		}

		ImGui::EndChild();

		ImGui::EndTable();
		return clicked;
	}

	std::optional<std::size_t> CostView::Draw(const Session& session, const std::optional<SessionSummary>& summary,
											  const RequestTimeline& timeline, const Pricing& pricing,
											  std::optional<std::size_t> selected, const SessionPalette& palette)
	{
		const std::vector<TurnCost> turns = TurnCosts(session, pricing);

		// Another reading of the transcript: where the reader had panned to, and the turn they
		// had open, named other turns.
		if (session.GetGeneration() != m_BuiltGeneration)
		{
			m_XAxis.Reset();
			m_SelectedTurn.reset();
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
									SingleLineLabel(session.GetTurns()[index].Prompt, kTooltipPromptBytes),
								turn.Known});

				CostResult cost;
				cost.Dollars = turn.Buckets.Total();

				if (!turn.Known)
				{
					cost.UnknownModels.emplace_back();
				}

				costs.push_back(std::move(cost));
			}

			FitTo(layers.size());

			// The turn open below is banded, and a click on a bar opens that turn, as its row does.
			if (const std::optional<std::size_t> bar = DrawChart("Cost per turn", "Turn", layers, CumulativeCost(costs),
																 bars, m_SelectedTurn, palette, m_XAxis))
			{
				m_SelectedTurn = bar;
			}
		}
		else
		{
			if (timeline.GetVersion() != m_BuiltVersion || pricing.GetEntries() != m_PricedWith)
			{
				PriceRequests(timeline, pricing);
			}

			// A click picks the request for the Requests tab to show.
			FitTo(m_RequestLayers.size());
			clicked = DrawChart("Cost per request", "Request", m_RequestLayers, m_RequestTotal, m_RequestBars, selected,
								palette, m_XAxis);
		}

		ImGui::Spacing();

		// The turns fill the rest of the tab until one is picked, and then share it with where
		// that turn's money went, below a divider the reader drags, as the Requests tab shares
		// its table with a request's details. Inside a child of its own, padded, since the
		// children DrawSplitResults opens are not.
		const std::function<void()> drawList = [&] {
			if (ImGui::BeginChild("##turnList", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding,
								  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
			{
				if (const std::optional<std::size_t> row = DrawTurnTable(session, turns, m_SelectedTurn, palette))
				{
					m_SelectedTurn = row;
				}
			}

			ImGui::EndChild();
		};

		const std::function<void()> drawDetails = [&] {
			if (const std::optional<std::size_t> request = DrawTurnDetails(session, turns, timeline, pricing, palette))
			{
				clicked = request;
			}
		};

		DrawSplitResults(SplitResultsInput{.HasSelection = m_SelectedTurn && *m_SelectedTurn < turns.size(),
										   .ResultsHeight = m_ListHeight,
										   .DrawResults = drawList,
										   .DrawPreview = drawDetails,
										   .ResultsChildId = "##turnResults",
										   .PreviewChildId = "##turnDetails",
										   .SplitterId = "##turnSplit"});

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
			costs.push_back(pricing.CostOf(request));

			// Named as the Requests tab's table names it: its number, when it was answered, and
			// the model that answered.
			std::string heading = "#" + std::to_string(request.Index);

			if (const std::string time = m_Clock.Format(request.RespondedAt, ClockStyle::DateTime); !time.empty())
			{
				heading += kDot + time;
			}

			if (!request.Model.empty())
			{
				heading += kDot + ModelLabel(request.Model, request.Fast);
			}

			m_RequestBars.push_back({std::move(heading), costs.back().IsKnown()});
		}

		m_RequestTotal = CumulativeCost(costs);
		m_BuiltVersion = timeline.GetVersion();
		m_PricedWith = pricing.GetEntries();
	}
}
