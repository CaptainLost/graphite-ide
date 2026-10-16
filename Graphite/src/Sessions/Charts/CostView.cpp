#include "Sessions/Charts/CostView.h"

#include "Core/Base/Base.h"

#include "Core/Base/Narrow.h"
#include "Core/Base/Strings.h"

#include "Sessions/Requests/RequestRow.h"
#include "Sessions/SessionDrawing.h"
#include "Sessions/Charts/StackedBarChart.h"
#include "Sessions/SessionSummary.h"
#include "Sessions/SessionText.h"
#include "Sessions/Transcript/ToolFacts.h"
#include "UI/SplitResults.h"
#include "UI/TypographicMarks.h"
#include "UI/FittedText.h"

#include "Core/ImGui/EmptyState.h"
#include "Core/ImGui/ImGuiColors.h"

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {
		constexpr const char* kUnknownMark = "?";
		constexpr double kPercentScale = 100.0;
		constexpr int kModelTableColumns = 4;
		constexpr int kBucketTableColumns = 4;
		constexpr int kToolTableColumns = 3;
		constexpr int kTurnTableColumns = 5;
		constexpr int kTurnRequestTableColumns = 4;
		constexpr int kTurnDetailColumns = 2;
		constexpr int kFrozenColumns = 0;
		constexpr int kFrozenRows = 1;
		constexpr int kContextColumnIndex = 2;

		constexpr std::size_t kTooltipPromptBytes = 2000;
		constexpr std::size_t kPanePromptBytes = 20000;
		constexpr std::size_t kRowPromptBytes = 1000;
		constexpr std::size_t kToolNameBytes = 80;

		constexpr double kFirstBarEdge = 0.5;
		constexpr double kHalfSlot = 0.5;
		constexpr double kOneSlot = 1.0;

		constexpr float kSwatchHeightOfLine = 0.55f;
		constexpr float kRoomyPaddingEms = 0.25f;
		constexpr float kRoomyPaddingExtraY = 2.0f;
		constexpr float kTableBorderPixels = 2.0f;
		constexpr float kBarColumnEms = 8.0f;
		constexpr float kToolColumnMaxEms = 12.0f;
		constexpr float kOpenTurnWashAlpha = 0.35f;
		constexpr float kMinVisibleBarWidth = 1.0f;
		constexpr float kMinSectionLines = 3.0f;
		constexpr float kPromptBoxLines = 3.0f;
		constexpr float kMoneyColumnWeight = 2.0f;
		constexpr float kRequestsColumnWeight = 3.0f;
		constexpr float kWrapAtWindowEdge = 0.0f;
		constexpr float kHalf = 0.5f;

		constexpr double kNegligibleDollars = 0.01;

		constexpr const char* kLayerNames[kCostLayers] = {"Cache read", "Cache write", "Uncached input", "Output"};
		constexpr const char* kTotalSeries = "Total";
		constexpr const char* kPathSeparators = "/\\";

		constexpr CostLayers kUnpricedRequestLayers{};

		CostResult CostOfUsage(const Pricing& pricing, const UsageByModel& usage)
		{
			return pricing.CostOf(usage);
		}

		UsageByModel MainUsageOf(const Session& session, const std::optional<SessionSummary>& summary)
		{
			if (summary.has_value())
			{
				return summary->Main;
			}

			UsageByModel usage;

			for (const Turn& turn : session.GetTurns())
			{
				for (const auto& [modelKey, tokenCounts] : turn.Usage)
				{
					usage[modelKey] += tokenCounts;
				}
			}

			return usage;
		}

		void DrawTotals(const Session& session, const std::optional<SessionSummary>& summary, const Pricing& pricing)
		{
			const bool splitsOwnFromSubagents = summary.has_value();

			const UsageByModel mainUsage = MainUsageOf(session, summary);
			const CostResult ownCost = CostOfUsage(pricing, mainUsage);
			const CostResult subagentsCost =
				splitsOwnFromSubagents ? CostOfUsage(pricing, summary->Subagents) : CostResult{};

			CostResult totalCost = ownCost;
			totalCost += subagentsCost;

			if (splitsOwnFromSubagents)
			{
				ImGui::Text("Total %s   session %s   subagents %s", FormatCost(totalCost).c_str(),
							FormatCost(ownCost).c_str(), FormatCost(subagentsCost).c_str());
			}
			else
			{
				ImGui::Text("Total %s", FormatCost(totalCost).c_str());
			}

			if (!totalCost.IsKnown())
			{
				std::string names;
				for (const std::string& model : totalCost.UnknownModels)
				{
					Core::AppendSeparated(names, ", ", model);
				}

				ImGui::TextDisabled("No price for %s. Add one under Settings > Claude.", names.c_str());
			}

			if (!ImGui::BeginTable("##models", kModelTableColumns,
								   ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
			{
				return;
			}

			ImGui::TableSetupColumn("Model");
			ImGui::TableSetupColumn("Tokens");
			ImGui::TableSetupColumn("Cost");
			ImGui::TableSetupColumn("Share");
			ImGui::TableHeadersRow();

			UsageByModel usageWithSubagents = mainUsage;
			if (splitsOwnFromSubagents)
			{
				for (const auto& [modelKey, tokenCounts] : summary->Subagents)
				{
					usageWithSubagents[modelKey] += tokenCounts;
				}
			}

			for (const auto& [modelKey, tokenCounts] : usageWithSubagents)
			{
				const CostResult modelCost = pricing.CostOf(modelKey, tokenCounts);

				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				const std::string modelLabel = ModelLabel(modelKey.Model, modelKey.Fast);
				ImGui::TextUnformatted(modelLabel.c_str());
				ImGui::SetItemTooltip("%s", modelKey.Model.c_str());
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(FormatTokens(tokenCounts.Total()).c_str());
				ImGui::SetItemTooltip("%s tokens", FormatTokensExact(tokenCounts.Total()).c_str());
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(FormatCost(modelCost).c_str());
				ImGui::TableNextColumn();

				const bool shareIsMeaningful = modelCost.IsKnown() && totalCost.IsKnown() && totalCost.Dollars > 0.0;
				if (shareIsMeaningful)
				{
					ImGui::Text("%.0f%%", kPercentScale * modelCost.Dollars / totalCost.Dollars);
				}
			}

			ImGui::EndTable();
		}

		void WrappedPrompt(const std::string& prompt)
		{
			std::string shownPrompt = TruncateUtf8(prompt, kTooltipPromptBytes);
			if (shownPrompt.size() < prompt.size())
			{
				shownPrompt += kEllipsis;
			}

			ImGui::PushTextWrapPos(ImGui::GetFontSize() * kTooltipEms);
			ImGui::TextUnformatted(shownPrompt.c_str(), shownPrompt.c_str() + shownPrompt.size());
			ImGui::PopTextWrapPos();
		}

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

		CostLayers CostLayersOf(const BucketCosts& buckets)
		{
			CostLayers layers{};
			layers[static_cast<std::size_t>(CostLayer::CacheRead)] = buckets.CacheRead;
			layers[static_cast<std::size_t>(CostLayer::CacheWrite)] = buckets.CacheWrite;
			layers[static_cast<std::size_t>(CostLayer::Input)] = buckets.Input;
			layers[static_cast<std::size_t>(CostLayer::Output)] = buckets.Output;
			return layers;
		}

		std::string DollarsText(double dollars)
		{
			CostResult cost;
			cost.Dollars = dollars;
			return FormatCost(cost);
		}

		std::string KnownDollarsText(bool known, double dollars)
		{
			return known ? DollarsText(dollars) : std::string{kUnknownMark};
		}

		void AssignIfPresent(std::optional<std::size_t>& target, const std::optional<std::size_t>& candidate)
		{
			if (candidate.has_value())
			{
				target = candidate;
			}
		}

		void DrawBarTooltip(const CostBar& bar, const CostLayers& barLayers, double sessionTotal,
							const SessionPalette& palette)
		{
			if (!ImGui::BeginTooltip())
			{
				return;
			}

			const ImU32 textColour = ImGui::GetColorU32(ImGuiCol_Text);

			ImGui::PushTextWrapPos(ImGui::GetFontSize() * kTooltipEms);
			ColouredText(textColour, bar.Heading);
			ImGui::PopTextWrapPos();

			double barCost = 0.0;
			for (const double layerDollars : barLayers)
			{
				barCost += layerDollars;
			}

			ColouredText(textColour, "Cost " + KnownDollarsText(bar.Known, barCost));

			std::array<SwatchRow, kCostLayers> rows;
			for (std::size_t layer = 0; layer < kCostLayers; ++layer)
			{
				rows[layer] = {.Colour = LayerColour(palette, layer),
							   .Label = kLayerNames[layer],
							   .Number = KnownDollarsText(bar.Known, barLayers[layer])};
			}

			DrawSwatchRows(rows, textColour);

			ColouredText(palette.TextDisabled, "Session so far " + DollarsText(sessionTotal));

			ImGui::EndTooltip();
		}

		std::optional<std::size_t> DrawChart(const char* title, const char* axisLabel,
											 const std::vector<CostLayers>& layersPerBar,
											 const std::vector<double>& runningTotals, const std::vector<CostBar>& bars,
											 std::optional<std::size_t> selectedBar, const SessionPalette& palette,
											 FollowingAxis& followingAxis)
		{
			const int barCount = Core::Narrow<int>(layersPerBar.size());

			if (barCount == 0)
			{
				Core::EmptyState("Nothing to chart yet.", nullptr, ImVec2(0.0f, ImGui::GetFontSize() * kChartLines));
				return std::nullopt;
			}

			if (!BeginBarChart(title, "costChart"))
			{
				return std::nullopt;
			}

			const double lastBarEdge = static_cast<double>(barCount) + kHalfSlot;

			ImPlot::SetupAxis(ImAxis_X1, axisLabel, ImPlotAxisFlags_None);
			SetupCountedAxis();
			followingAxis.Setup(kFirstBarEdge, lastBarEdge);

			FinishBarChartSetup({.BarCaption = "Dollars", .LineCaption = "Total dollars", .LineFromZero = true});

			if (selectedBar.has_value() && *selectedBar < layersPerBar.size())
			{
				DrawColumnBand(static_cast<double>(*selectedBar + 1), kOneSlot, palette.Accent);
			}

			std::array<bool, kCostLayers> layerShown{};
			for (std::size_t layer = 0; layer < kCostLayers; ++layer)
			{
				layerShown[layer] = SeriesShown(kLayerNames[layer]);
			}

			std::vector<double> barPositions(layersPerBar.size());
			std::array<std::vector<double>, kCostLayers> topsByLayer;

			for (std::vector<double>& layerTops : topsByLayer)
			{
				layerTops.resize(layersPerBar.size());
			}

			for (std::size_t barIndex = 0; barIndex < layersPerBar.size(); ++barIndex)
			{
				barPositions[barIndex] = static_cast<double>(barIndex + 1);
				const CostLayers stackedTops = StackedTops(layersPerBar[barIndex], layerShown);

				for (std::size_t layer = 0; layer < kCostLayers; ++layer)
				{
					topsByLayer[layer][barIndex] = stackedTops[layer];
				}
			}

			std::array<BarLayer, kCostLayers> barLayers{};
			for (std::size_t layer = 0; layer < kCostLayers; ++layer)
			{
				barLayers[layer] = {.Id = kLayerNames[layer],
									.Colour = ImGui::ColorConvertU32ToFloat4(LayerColour(palette, layer)),
									.Tops = topsByLayer[layer]};
			}

			PlotStackedBars(barLayers, barPositions, kBarFraction);

			ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
			ImPlot::SetNextLineStyle(ImGui::ColorConvertU32ToFloat4(palette.Accent));
			ImPlot::PlotLine(kTotalSeries, barPositions.data(), runningTotals.data(), barCount);

			followingAxis.Track();

			std::optional<std::size_t> clickedBar;

			if (ImPlot::IsPlotHovered())
			{
				const double mousePlotX = ImPlot::GetPlotMousePos(ImAxis_X1, ImAxis_Y1).x;
				const double nearestBarX = std::clamp(std::round(mousePlotX), 1.0, static_cast<double>(barCount));
				const std::size_t nearestBarIndex = static_cast<std::size_t>(nearestBarX) - 1;

				const bool overBars = mousePlotX >= kFirstBarEdge && mousePlotX <= lastBarEdge;
				if (overBars && ColumnClicked())
				{
					clickedBar = nearestBarIndex;
				}

				const bool overBarItself = std::abs(mousePlotX - nearestBarX) <= kBarFraction * kHalfSlot;
				if (!ChartPanning() && overBarItself && nearestBarIndex < bars.size())
				{
					DrawBarTooltip(bars[nearestBarIndex], layersPerBar[nearestBarIndex], runningTotals[nearestBarIndex],
								   palette);
				}
			}

			EndBarChart();
			return clickedBar;
		}

		float SwatchSize()
		{
			return std::round(ImGui::GetTextLineHeight() * kSwatchHeightOfLine);
		}

		void Swatch(ImU32 colour)
		{
			const ImVec2 cursorPosition = ImGui::GetCursorScreenPos();
			const float swatchSize = SwatchSize();
			const float top = std::floor(cursorPosition.y + (ImGui::GetTextLineHeight() - swatchSize) * kHalf);
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(cursorPosition.x, top),
													  ImVec2(cursorPosition.x + swatchSize, top + swatchSize), colour);
			ImGui::Dummy(ImVec2(swatchSize, ImGui::GetTextLineHeight()));
			ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
		}

		void MoveCursorToRightAlign(float width)
		{
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - width));
		}

		void RightAligned(ImU32 colour, const std::string& text)
		{
			const float width = ImGui::CalcTextSize(text.c_str()).x;
			MoveCursorToRightAlign(width);
			ColouredText(colour, text);
		}

		void RightHeaderInCell(const char* name)
		{
			const float width = ImGui::CalcTextSize(name).x;
			MoveCursorToRightAlign(width);
			ImGui::TableHeader(name);
		}

		ImVec2 RoomyCellPadding()
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			return ImVec2(style.CellPadding.x + ImGui::GetFontSize() * kRoomyPaddingEms,
						  style.CellPadding.y + kRoomyPaddingExtraY);
		}

		void MoneyCell(double dollars, bool known, const SessionPalette& palette)
		{
			ImGui::TableNextColumn();

			if (!known)
			{
				RightAligned(palette.TextDisabled, kUnknownMark);
				return;
			}

			const ImU32 textColour =
				dollars < kNegligibleDollars ? palette.TextDisabled : ImGui::GetColorU32(ImGuiCol_Text);
			RightAligned(textColour, DollarsText(dollars));
		}

		// WARNING: the padding stays pushed until EndRoomyTable. BeginTable takes its width once, but
		// every TableNextRow takes its height from the style again.
		bool BeginRoomyTable(const char* id, int columns, ImGuiTableFlags flags, ImVec2 size = ImVec2(0.0f, 0.0f))
		{
			ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, RoomyCellPadding());
			const bool isOpen = ImGui::BeginTable(id, columns, ImGuiTableFlags_RowBg | flags, size);

			if (!isOpen)
			{
				ImGui::PopStyleVar();
			}

			return isOpen;
		}

		void EndRoomyTable()
		{
			ImGui::EndTable();
			ImGui::PopStyleVar();
		}

		float CostWidth()
		{
			return std::max(ImGui::CalcTextSize("< $0.01").x, ImGui::CalcTextSize("$000.00").x);
		}

		void LineBar(float left, float right, ImU32 colour)
		{
			const float barHeight = SwatchSize();
			const float top =
				std::floor(ImGui::GetCursorScreenPos().y + (ImGui::GetTextLineHeight() - barHeight) * kHalf);
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(left, top), ImVec2(right, top + barHeight), colour);
		}

		// WARNING: each layer's ends are rounded from the running sum, so the pieces meet without a
		// gap. Rounding each width separately would bring the gaps back.
		void StackedBar(const CostLayers& layerCosts, double fullWidthCost, const SessionPalette& palette)
		{
			if (fullWidthCost <= 0.0)
			{
				return;
			}

			const float left = ImGui::GetCursorScreenPos().x;
			const double room = ImGui::GetContentRegionAvail().x;
			double runningSum = 0.0;

			for (std::size_t layer = 0; layer < kCostLayers; ++layer)
			{
				const float segmentStart = left + static_cast<float>(std::round(room * runningSum / fullWidthCost));
				runningSum += layerCosts[layer];
				const float segmentEnd = left + static_cast<float>(std::round(room * runningSum / fullWidthCost));

				if (segmentEnd > segmentStart)
				{
					LineBar(segmentStart, segmentEnd, LayerColour(palette, layer));
				}
			}
		}

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

		float RoomyRowHeight()
		{
			return ImGui::GetTextLineHeight() + RoomyCellPadding().y * 2.0f;
		}

		void DrawTurnFooter(const ColumnEdges& promptColumn, const ColumnEdges& requestsColumn,
							const ColumnEdges& costColumn, std::size_t requestCount, double dollars, bool known,
							const SessionPalette& palette)
		{
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y);

			const ImVec2 origin = ImGui::GetCursorScreenPos();
			const float width = ImGui::GetContentRegionAvail().x;
			const float height = RoomyRowHeight();
			ImDrawList* drawList = ImGui::GetWindowDrawList();

			drawList->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
									ImGui::GetColorU32(ImGuiCol_TableHeaderBg));
			drawList->AddLine(origin, ImVec2(origin.x + width, origin.y), ImGui::GetColorU32(ImGuiCol_TableBorderStrong));

			const ImU32 textColour = ImGui::GetColorU32(ImGuiCol_Text);
			const float y = origin.y + (height - ImGui::GetTextLineHeight()) * kHalf;

			ImVec2 piece(promptColumn.Left, y);
			DrawPiece(piece, textColour, "Total");

			const std::string requestCountText = std::to_string(requestCount);
			piece = ImVec2(requestsColumn.Right - ImGui::CalcTextSize(requestCountText.c_str()).x, y);
			DrawPiece(piece, textColour, requestCountText);

			const std::string costText = KnownDollarsText(known, dollars);
			piece = ImVec2(costColumn.Right - ImGui::CalcTextSize(costText.c_str()).x, y);
			DrawPiece(piece, known ? textColour : palette.TextDisabled, costText);

			ImGui::Dummy(ImVec2(width, height));
		}

		std::optional<std::size_t> DrawTurnTable(const Session& session, const std::vector<TurnCost>& turns,
												 std::optional<std::size_t> openTurn, const SessionPalette& palette)
		{
			const bool hasFooter = turns.size() > 1;

			float tableHeight = 0.0f;
			if (hasFooter)
			{
				const float headerAndRowsHeight =
					RoomyRowHeight() * static_cast<float>(turns.size() + 1) + kTableBorderPixels;
				const float heightLeftForTable = ImGui::GetContentRegionAvail().y - RoomyRowHeight();
				tableHeight = std::max(std::min(headerAndRowsHeight, heightLeftForTable), RoomyRowHeight() * 2.0f);
			}

			if (!BeginRoomyTable("##turns", kTurnTableColumns,
								 ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_ScrollY | ImGuiTableFlags_PadOuterX,
								 ImVec2(0.0f, tableHeight)))
			{
				return std::nullopt;
			}

			ImGui::TableSetupScrollFreeze(kFrozenColumns, kFrozenRows);

			// WARNING: the prompt takes whatever the numbers leave. A table that cannot be resized gives
			// a fixed column the width it is set up with every frame, so the numbers' columns follow the
			// font; were the prompt's width taken from its text, a prompt cut to the column would narrow
			// the column.
			ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Prompt", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Requests", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Requests").x);
			ImGui::TableSetupColumn("##bar", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * kBarColumnEms);
			ImGui::TableSetupColumn("Cost", ImGuiTableColumnFlags_WidthFixed, CostWidth());

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

			double dearestCost = 0.0;
			for (const TurnCost& turnCost : turns)
			{
				dearestCost = std::max(dearestCost, turnCost.Buckets.Total());
			}

			const ImU32 textColour = ImGui::GetColorU32(ImGuiCol_Text);
			std::optional<std::size_t> clickedTurn;

			ImGuiListClipper clipper;
			clipper.Begin(Core::Narrow<int>(turns.size()));

			while (clipper.Step())
			{
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
				{
					const std::size_t turnIndex = static_cast<std::size_t>(row);
					const TurnCost& turnCost = turns[turnIndex];
					const Turn& turn = session.GetTurns()[turnIndex];

					ImGui::TableNextRow();
					ImGui::TableNextColumn();

					ImGui::PushID(row);
					const ImVec4 openTurnWash =
						Core::WithAlphaReplaced(ImGui::ColorConvertU32ToFloat4(palette.Accent), kOpenTurnWashAlpha);
					ImGui::PushStyleColor(ImGuiCol_Header, openTurnWash);
					if (ImGui::Selectable("##row", openTurn == turnIndex,
										  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
					{
						clickedTurn = turnIndex;
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
						ColouredText(textColour, FitToWidth(SingleLineLabel(turn.Prompt, kRowPromptBytes),
															ImGui::GetContentRegionAvail().x));
					}

					ImGui::TableNextColumn();
					RightAligned(turn.Requests.empty() ? palette.TextDisabled : textColour,
								 std::to_string(turn.Requests.size()));

					ImGui::TableNextColumn();
					StackedBar(CostLayersOf(turnCost.Buckets), dearestCost, palette);

					MoneyCell(turnCost.Buckets.Total(), turnCost.Known, palette);
				}
			}

			EndRoomyTable();

			if (hasFooter)
			{
				bool allKnown = true;
				double totalDollars = 0.0;
				std::size_t totalRequests = 0;

				for (std::size_t turnIndex = 0; turnIndex < turns.size(); ++turnIndex)
				{
					allKnown = allKnown && turns[turnIndex].Known;
					totalDollars += turns[turnIndex].Buckets.Total();
					totalRequests += session.GetTurns()[turnIndex].Requests.size();
				}

				DrawTurnFooter(promptEdges, requestsEdges, costEdges, totalRequests, totalDollars, allKnown, palette);
			}

			return clickedTurn;
		}

		class DearestLayerFirst
		{
		public:
			explicit DearestLayerFirst(const CostLayers& layerCosts) : m_LayerCosts(layerCosts) {}

			bool operator()(std::size_t firstLayer, std::size_t secondLayer) const
			{
				return m_LayerCosts[firstLayer] > m_LayerCosts[secondLayer];
			}

		private:
			const CostLayers& m_LayerCosts;
		};

		void DrawBuckets(const TurnCost& turnCost, const SessionPalette& palette)
		{
			const CostLayers layerCosts = CostLayersOf(turnCost.Buckets);
			const double turnTotal = turnCost.Buckets.Total();

			std::array<std::size_t, kCostLayers> layersByCost{};
			for (std::size_t layer = 0; layer < kCostLayers; ++layer)
			{
				layersByCost[layer] = layer;
			}
			std::ranges::stable_sort(layersByCost, DearestLayerFirst(layerCosts));

			if (!ImGui::BeginTable("##buckets", kBucketTableColumns, ImGuiTableFlags_None))
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

			const ImU32 textColour = ImGui::GetColorU32(ImGuiCol_Text);

			for (const std::size_t layer : layersByCost)
			{
				const double share = turnTotal > 0.0 ? layerCosts[layer] / turnTotal : 0.0;
				const bool isQuiet = !(turnCost.Known && layerCosts[layer] >= kNegligibleDollars);
				const ImU32 rowColour = isQuiet ? palette.TextDisabled : textColour;

				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				Swatch(LayerColour(palette, layer));
				ColouredText(rowColour, kLayerNames[layer]);

				MoneyCell(layerCosts[layer], turnCost.Known, palette);

				ImGui::TableNextColumn();
				if (turnCost.Known)
				{
					RightAligned(rowColour, std::format("{:.0f}%", kPercentScale * share));
				}

				ImGui::TableNextColumn();
				if (turnCost.Known)
				{
					const float left = ImGui::GetCursorScreenPos().x;
					const float width = std::round(ImGui::GetContentRegionAvail().x * static_cast<float>(share));

					if (width >= kMinVisibleBarWidth)
					{
						LineBar(left, left + width, LayerColour(palette, layer));
					}
				}
			}

			ImGui::EndTable();
		}

		void DrawTools(const std::vector<ToolCount>& tools, const SessionPalette& palette)
		{
			if (tools.empty())
			{
				ColouredText(palette.TextDisabled, "No tools called.");
				return;
			}

			if (!ImGui::BeginTable("##tools", kToolTableColumns, ImGuiTableFlags_None))
			{
				return;
			}

			std::size_t mostCalls = 0;
			float nameWidth = 0.0f;
			std::vector<std::string> displayNames;
			displayNames.reserve(tools.size());

			for (const ToolCount& tool : tools)
			{
				mostCalls = std::max(mostCalls, tool.Count);
				displayNames.push_back(SingleLineLabel(tool.Name, kToolNameBytes));
				nameWidth = std::max(nameWidth, ImGui::CalcTextSize(displayNames.back().c_str()).x);
			}

			const float iconWidth = ImGui::CalcTextSize(ICON_MDI_CONSOLE).x + ImGui::GetStyle().ItemInnerSpacing.x;
			const std::string widestCountText = std::format("{}000", kMultiplicationSign);
			ImGui::TableSetupColumn("Tool", ImGuiTableColumnFlags_WidthFixed,
									std::min(iconWidth + nameWidth, ImGui::GetFontSize() * kToolColumnMaxEms));
			ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed,
									ImGui::CalcTextSize(widestCountText.c_str()).x);
			ImGui::TableSetupColumn("Bar", ImGuiTableColumnFlags_WidthStretch);

			const ImU32 textColour = ImGui::GetColorU32(ImGuiCol_Text);

			for (std::size_t toolIndex = 0; toolIndex < tools.size(); ++toolIndex)
			{
				const ToolCount& tool = tools[toolIndex];
				const ToolCategory category = CategoryOf(tool.Name);
				const ImU32 categoryColour = palette.CategoryColor(category);

				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ColouredText(categoryColour, IconOf(category));
				ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
				ColouredText(textColour, FitToWidth(displayNames[toolIndex], ImGui::GetContentRegionAvail().x));

				ImGui::TableNextColumn();
				RightAligned(textColour, std::format("{}{}", kMultiplicationSign, tool.Count));

				ImGui::TableNextColumn();
				const float left = ImGui::GetCursorScreenPos().x;
				const float width = std::round(ImGui::GetContentRegionAvail().x * static_cast<float>(tool.Count) /
											   static_cast<float>(mostCalls));

				const ImU32 quietBarColour = palette.TextDisabled;
				if (width >= kMinVisibleBarWidth)
				{
					LineBar(left, left + width, quietBarColour);
				}
			}

			ImGui::EndTable();
		}

		std::string Compacted(const std::string& line)
		{
			const std::size_t firstSpace = line.find(' ');
			if (firstSpace == std::string::npos)
			{
				return line;
			}

			const std::string_view afterName = std::string_view{line}.substr(firstSpace + 1);
			const std::size_t lastSeparator = afterName.find_last_of(kPathSeparators);

			const bool hasSpaceAfterName = afterName.find(' ') != std::string_view::npos;
			const bool hasNoPath = lastSeparator == std::string_view::npos;
			const bool endsWithSeparator = lastSeparator + 1 == afterName.size();

			if (hasSpaceAfterName || hasNoPath || endsWithSeparator)
			{
				return line;
			}

			return line.substr(0, firstSpace + 1) + std::string{afterName.substr(lastSeparator + 1)};
		}

		struct ActivityCount
		{
			std::string Brief;
			std::size_t Count = 1;
		};

		std::string ShortActivity(const std::vector<std::string>& activity)
		{
			std::vector<ActivityCount> counted;

			for (const std::string& line : activity)
			{
				std::string brief = Compacted(line);

				const bool isNeitherFileNorReply = brief == line && !line.starts_with(kLeftDoubleQuotationMark);
				if (isNeitherFileNorReply)
				{
					brief = line.substr(0, line.find(' '));
				}

				ActivityCount* seen = nullptr;
				for (ActivityCount& entry : counted)
				{
					if (entry.Brief == brief)
					{
						seen = &entry;
						break;
					}
				}

				if (seen == nullptr)
				{
					counted.push_back({.Brief = std::move(brief), .Count = 1});
				}
				else
				{
					++seen->Count;
				}
			}

			std::string text;
			for (const ActivityCount& entry : counted)
			{
				std::string piece = entry.Brief;
				if (entry.Count > 1)
				{
					piece += std::format(" {}{}", kMultiplicationSign, entry.Count);
				}

				Core::AppendSeparated(text, ", ", piece);
			}

			return text;
		}

		// WARNING: EndChild must be called whether or not this returns true.
		bool BeginSection(const char* title, const char* id)
		{
			ImGui::SeparatorText(title);

			const float verticalCellPadding = ImGui::GetStyle().CellPadding.y * 2.0f;
			const float sectionHeight = std::max(ImGui::GetContentRegionAvail().y - verticalCellPadding,
												 ImGui::GetTextLineHeightWithSpacing() * kMinSectionLines);
			return ImGui::BeginChild(id, ImVec2(0.0f, sectionHeight));
		}

		void DrawPromptBox(const std::string& text, const SessionPalette& palette)
		{
			std::string prompt = TruncateUtf8(text, kPanePromptBytes);
			if (prompt.size() < text.size())
			{
				prompt += kEllipsis;
			}

			constexpr bool keepDoubleHashInText = false;

			const ImGuiStyle& style = ImGui::GetStyle();
			const float wrapWidth = ImGui::GetContentRegionAvail().x - style.FramePadding.x * 2.0f;
			const float textHeight =
				prompt.empty()
					? ImGui::GetTextLineHeight()
					: ImGui::CalcTextSize(prompt.c_str(), prompt.c_str() + prompt.size(), keepDoubleHashInText, wrapWidth)
						  .y;
			const float boxHeight =
				std::min(textHeight, ImGui::GetTextLineHeight() * kPromptBoxLines) + style.FramePadding.y * 2.0f;

			if (ImGui::BeginChild("##turnPrompt", ImVec2(0.0f, boxHeight), ImGuiChildFlags_FrameStyle))
			{
				if (prompt.empty())
				{
					ColouredText(palette.TextDisabled, "(no prompt)");
				}
				else
				{
					ImGui::PushTextWrapPos(kWrapAtWindowEdge);
					ImGui::TextUnformatted(prompt.c_str(), prompt.c_str() + prompt.size());
					ImGui::PopTextWrapPos();
				}
			}

			ImGui::EndChild();
		}

		void DrawTurnListChild(const Session& session, const std::vector<TurnCost>& turns,
							   std::optional<std::size_t>& openTurn, const SessionPalette& palette)
		{
			if (ImGui::BeginChild("##turnList", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding,
								  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
			{
				AssignIfPresent(openTurn, DrawTurnTable(session, turns, openTurn, palette));
			}

			ImGui::EndChild();
		}

		CostResult TurnCostAsResult(const TurnCost& turnCost)
		{
			CostResult result;
			result.Dollars = turnCost.Buckets.Total();

			if (!turnCost.Known)
			{
				result.UnknownModels.emplace_back();
			}

			return result;
		}

		struct TurnRequestRow
		{
			const TurnRequest* Request = nullptr;
			const ApiRequest* TimelineRequest = nullptr;
		};

		class RequestRowOrder
		{
		public:
			RequestRowOrder(bool byContext, bool descending) : m_ByContext(byContext), m_Descending(descending) {}

			bool operator()(const TurnRequestRow& first, const TurnRequestRow& second) const
			{
				if ((first.TimelineRequest != nullptr) != (second.TimelineRequest != nullptr))
				{
					return first.TimelineRequest != nullptr;
				}
				if (first.TimelineRequest == nullptr)
				{
					return false;
				}

				const std::uint64_t firstKey = m_ByContext ? first.TimelineRequest->Context : first.TimelineRequest->Index;
				const std::uint64_t secondKey =
					m_ByContext ? second.TimelineRequest->Context : second.TimelineRequest->Index;
				return m_Descending ? firstKey > secondKey : firstKey < secondKey;
			}

		private:
			bool m_ByContext;
			bool m_Descending;
		};
	}

	std::optional<std::size_t> CostView::DrawTurnDetails(const Session& session, const std::vector<TurnCost>& turns,
														 const RequestTimeline& timeline, const Pricing& pricing,
														 const SessionPalette& palette)
	{
		if (!m_SelectedTurn || *m_SelectedTurn >= turns.size())
		{
			return std::nullopt;
		}

		const std::size_t turnIndex = *m_SelectedTurn;
		const TurnCost& turnCost = turns[turnIndex];
		const Turn& turn = session.GetTurns()[turnIndex];
		const ImU32 textColour = ImGui::GetColorU32(ImGuiCol_Text);

		const std::string heading =
			std::format("Turn {}{}{}{}{}", turnIndex + 1, kMiddleDotSeparator,
						KnownDollarsText(turnCost.Known, turnCost.Buckets.Total()), kMiddleDotSeparator,
						Core::Counted(turn.Requests.size(), "request"));
		constexpr Core::IconButtonFit kOnTheTextLine = Core::IconButtonFit::Line;

		const float headingWidth = std::max(ImGui::GetContentRegionAvail().x - PaneCloseRoom(kOnTheTextLine), 0.0f);
		ColouredText(textColour, FitToWidth(heading, headingWidth));

		if (DrawPaneClose(kOnTheTextLine))
		{
			m_SelectedTurn.reset();
			return std::nullopt;
		}

		ImGui::Spacing();

		const TurnBreakdown breakdown = BreakDownTurn(session, turnIndex, timeline, pricing);
		std::optional<std::size_t> clickedRequest;

		DrawPromptBox(turn.Prompt, palette);
		ImGui::Spacing();

		if (!ImGui::BeginTable("##turnDetailColumns", kTurnDetailColumns,
							   ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX))
		{
			return std::nullopt;
		}

		ImGui::TableSetupColumn("Money", ImGuiTableColumnFlags_WidthStretch, kMoneyColumnWeight);
		ImGui::TableSetupColumn("Requests", ImGuiTableColumnFlags_WidthStretch, kRequestsColumnWeight);

		ImGui::TableNextRow();
		ImGui::TableNextColumn();

		if (BeginSection("Where it went", "##turnMoney"))
		{
			DrawBuckets(turnCost, palette);
			ImGui::Spacing();
			ImGui::SeparatorText("Tools");
			DrawTools(breakdown.Tools, palette);
		}

		ImGui::EndChild();

		ImGui::TableNextColumn();

		if (BeginSection("Requests", "##turnRequestList"))
		{
			const std::vector<ApiRequest>& timelineRequests = timeline.GetRequests();

			std::vector<TurnRequestRow> requestRows;
			requestRows.reserve(breakdown.Requests.size());

			for (const TurnRequest& request : breakdown.Requests)
			{
				const ApiRequest* timelineRequest = nullptr;
				if (request.Position && *request.Position < timelineRequests.size())
				{
					timelineRequest = &timelineRequests[*request.Position];
				}

				requestRows.push_back({.Request = &request, .TimelineRequest = timelineRequest});
			}

			if (requestRows.empty())
			{
				ColouredText(palette.TextDisabled, "No requests.");
			}
			else if (BeginRoomyTable("##turnRequests", kTurnRequestTableColumns,
									 ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersOuterH))
			{
				const float sortArrowWidth = ImGui::GetFontSize();

				ImGui::TableSetupScrollFreeze(kFrozenColumns, kFrozenRows);
				ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort,
										ImGui::CalcTextSize("#0000").x + sortArrowWidth);
				ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort,
										ImGui::CalcTextSize("00:00:00").x);
				ImGui::TableSetupColumn("Context",
										ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending,
										std::max(ImGui::CalcTextSize("000.0k").x, ImGui::CalcTextSize("Context").x) +
											sortArrowWidth);
				ImGui::TableSetupColumn("Response", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort);
				ImGui::TableHeadersRow();

				const ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs();
				const bool hasSortSpec = sortSpecs != nullptr && sortSpecs->SpecsCount > 0;
				const bool byContext = hasSortSpec && sortSpecs->Specs[0].ColumnIndex == kContextColumnIndex;
				const bool descending = hasSortSpec && sortSpecs->Specs[0].SortDirection == ImGuiSortDirection_Descending;

				std::ranges::stable_sort(requestRows, RequestRowOrder(byContext, descending));

				ImGuiListClipper clipper;
				clipper.Begin(Core::Narrow<int>(requestRows.size()));

				while (clipper.Step())
				{
					for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
					{
						const TurnRequestRow& requestRow = requestRows[static_cast<std::size_t>(row)];
						const TurnRequest* request = requestRow.Request;
						const ApiRequest* timelineRequest = requestRow.TimelineRequest;
						const std::vector<std::string> activity =
							timelineRequest != nullptr ? RequestActivity(*timelineRequest) : std::vector<std::string>{};

						ImGui::TableNextRow();
						ImGui::TableNextColumn();

						constexpr bool notSelected = false;

						ImGui::PushID(row);
						ImGui::BeginDisabled(timelineRequest == nullptr);
						if (ImGui::Selectable("##request", notSelected,
											  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
						{
							clickedRequest = request->Position;
						}
						ImGui::EndDisabled();
						ImGui::PopID();

						if (timelineRequest != nullptr && ImGui::BeginItemTooltip())
						{
							ImGui::TextUnformatted(
								RequestLine(*timelineRequest, pricing, m_Clock, ClockStyle::DateTime).c_str());

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

						ImGui::SameLine(0.0f, 0.0f);
						ColouredText(palette.TextDisabled, timelineRequest != nullptr
															   ? std::to_string(timelineRequest->Index)
															   : std::string{kUnknownMark});

						ImGui::TableNextColumn();
						if (timelineRequest != nullptr)
						{
							ColouredText(palette.TextDisabled,
										 m_Clock.Format(timelineRequest->RespondedAt, ClockStyle::Time));
						}

						ImGui::TableNextColumn();
						if (timelineRequest != nullptr)
						{
							RightAligned(textColour, FormatTokens(timelineRequest->Context));
						}

						ImGui::TableNextColumn();
						ColouredText(textColour, FitToWidth(ShortActivity(activity), ImGui::GetContentRegionAvail().x));
					}
				}

				EndRoomyTable();
			}
		}

		ImGui::EndChild();

		ImGui::EndTable();
		return clickedRequest;
	}

	std::optional<std::size_t> CostView::Draw(const Session& session, const std::optional<SessionSummary>& summary,
											  const RequestTimeline& timeline, const Pricing& pricing,
											  std::optional<std::size_t> selectedRequest, const SessionPalette& palette)
	{
		const std::vector<TurnCost> turns = TurnCosts(session, pricing);

		if (session.GetGeneration() != m_BuiltGeneration)
		{
			m_XAxis.Reset();
			m_SelectedTurn.reset();
			m_BuiltGeneration = session.GetGeneration();
		}

		DrawTotals(session, summary, pricing);
		ImGui::Spacing();

		constexpr const char* kScopes[] = {"Per turn", "Per request"};

		int scopeIndex = static_cast<int>(m_By);

		if (SegmentedChoice(kScopes, scopeIndex))
		{
			m_By = static_cast<CostBy>(scopeIndex);
			m_XAxis.Reset();
		}

		std::optional<std::size_t> clickedRequest;

		if (m_By == CostBy::Turn)
		{
			std::vector<CostLayers> layersPerTurn;
			std::vector<CostResult> costsPerTurn;
			std::vector<CostBar> barsPerTurn;
			layersPerTurn.reserve(turns.size());
			costsPerTurn.reserve(turns.size());
			barsPerTurn.reserve(turns.size());

			for (std::size_t turnIndex = 0; turnIndex < turns.size(); ++turnIndex)
			{
				const TurnCost& turnCost = turns[turnIndex];
				layersPerTurn.push_back(CostLayersOf(turnCost.Buckets));

				barsPerTurn.push_back(
					{.Heading = std::format("Turn {}{}{}", turnIndex + 1, kMiddleDotSeparator,
											SingleLineLabel(session.GetTurns()[turnIndex].Prompt, kTooltipPromptBytes)),
					 .Known = turnCost.Known});

				costsPerTurn.push_back(TurnCostAsResult(turnCost));
			}

			FitTo(layersPerTurn.size());

			AssignIfPresent(m_SelectedTurn, DrawChart("Cost per turn", "Turn", layersPerTurn,
													  CumulativeCost(costsPerTurn), barsPerTurn, m_SelectedTurn,
													  palette, m_XAxis));
		}
		else
		{
			if (timeline.GetVersion() != m_BuiltVersion || pricing.GetEntries() != m_PricedWith)
			{
				PriceRequests(timeline, pricing);
			}

			FitTo(m_RequestLayers.size());
			clickedRequest = DrawChart("Cost per request", "Request", m_RequestLayers, m_RequestTotal, m_RequestBars,
									   selectedRequest, palette, m_XAxis);
		}

		ImGui::Spacing();

		const std::function<void()> drawTurnList = [&] { DrawTurnListChild(session, turns, m_SelectedTurn, palette); };

		const std::function<void()> drawTurnDetails = [&] {
			AssignIfPresent(clickedRequest, DrawTurnDetails(session, turns, timeline, pricing, palette));
		};

		const bool hasOpenTurn = m_SelectedTurn && *m_SelectedTurn < turns.size();

		DrawSplitResults(SplitResultsInput{.HasSelection = hasOpenTurn,
										   .ResultsHeight = m_ListHeight,
										   .DrawResults = drawTurnList,
										   .DrawPreview = drawTurnDetails,
										   .ResultsChildId = "##turnResults",
										   .PreviewChildId = "##turnDetails",
										   .SplitterId = "##turnSplit"});

		return clickedRequest;
	}

	void CostView::FitTo(std::size_t count)
	{
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

		for (const ApiRequest& request : requests)
		{
			m_RequestLayers.push_back(CostLayersOf(request, pricing).value_or(kUnpricedRequestLayers));
			costs.push_back(pricing.CostOf(request));

			std::string heading = "#" + std::to_string(request.Index);

			const std::string respondedTime = m_Clock.Format(request.RespondedAt, ClockStyle::DateTime);
			Core::AppendSeparatedIfNotEmpty(heading, kMiddleDotSeparator, respondedTime);

			if (!request.Model.empty())
			{
				Core::AppendSeparated(heading, kMiddleDotSeparator, ModelLabel(request.Model, request.Fast));
			}

			m_RequestBars.push_back({.Heading = std::move(heading), .Known = costs.back().IsKnown()});
		}

		m_RequestTotal = CumulativeCost(costs);
		m_BuiltVersion = timeline.GetVersion();
		m_PricedWith = pricing.GetEntries();
	}
}
