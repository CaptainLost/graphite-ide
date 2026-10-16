#include "Sessions/Requests/RequestsView.h"

#include "Core/Base/Base.h"

#include "Core/Base/Narrow.h"
#include "Core/Base/Strings.h"
#include "Core/ImGui/EmptyState.h"

#include "Sessions/Requests/RequestRow.h"
#include "Sessions/SessionDrawing.h"
#include "Sessions/Requests/SentTextLayout.h"
#include "Sessions/SessionText.h"
#include "Sessions/Transcript/ToolFacts.h"
#include "UI/SideBySide.h"
#include "UI/SplitResults.h"
#include "UI/TickBox.h"
#include "UI/TypographicMarks.h"
#include "UI/FittedText.h"
#include "UI/Lists/ListRow.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

namespace Graphite {

	namespace {
		constexpr std::size_t kToolNameBytes = 80;
		constexpr std::size_t kModelLabelBytes = 80;
		constexpr std::size_t kStopReasonBytes = 60;
		constexpr std::size_t kTooltipPromptBytes = 2000;

		constexpr float kToolFilterWidthInFonts = 14.0f;
		constexpr float kTurnMarkWidthInFonts = 0.2f;
		constexpr float kTurnMarkMinimumWidth = 2.0f;
		constexpr float kTurnTooltipWrapInFonts = 35.0f;
		constexpr float kSplitHandleWidthInFonts = 0.4f;
		constexpr float kSplitHandleMinimumWidth = 4.0f;
		constexpr float kSplitHandleCentre = 0.5f;
		constexpr float kMinimumPaneWidthInFonts = 12.0f;
		constexpr float kOpeningItemsShare = 0.4f;
		constexpr float kRevealScrollRatio = 0.5f;

		enum class CellAlignment
		{
			Left,
			Right
		};

		struct ColumnSpec
		{
			const char* Label;
			RequestColumn Id;
			ImGuiTableColumnFlags Flags;
			float Weight;
			bool Numeric;
		};

		constexpr ColumnSpec kColumns[] = {
			{"#", RequestColumn::Index, ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_NoHide, 0.6f, true},
			{"Time", RequestColumn::Time, ImGuiTableColumnFlags_None, 1.1f, false},
			{"Model", RequestColumn::Model, ImGuiTableColumnFlags_None, 1.2f, false},
			{"Context", RequestColumn::Context, ImGuiTableColumnFlags_None, 1.0f, true},
			{"\xce\x94", RequestColumn::Delta, ImGuiTableColumnFlags_None, 0.9f, true},
			{"Cache %", RequestColumn::CachedShare, ImGuiTableColumnFlags_None, 0.8f, true},
			{"Out", RequestColumn::Output, ImGuiTableColumnFlags_None, 0.8f, true},
			{"Cost", RequestColumn::Cost, ImGuiTableColumnFlags_None, 0.8f, true},
			{"Wait", RequestColumn::Wait, ImGuiTableColumnFlags_None, 0.8f, true},
			{"Stop", RequestColumn::Stop, ImGuiTableColumnFlags_None, 1.2f, false},
			{"Tools", RequestColumn::Tools, ImGuiTableColumnFlags_None, 2.0f, false},
			{ICON_MDI_CACHED "##rebuilt", RequestColumn::Rebuilt, ImGuiTableColumnFlags_WidthFixed, 0.0f, false},
		};

		constexpr int kColumnCount = static_cast<int>(std::size(kColumns));

		CellAlignment AlignmentOf(const ColumnSpec& columnSpec)
		{
			return columnSpec.Numeric ? CellAlignment::Right : CellAlignment::Left;
		}

		// WARNING: Every path ends on an item, the empty text included, because the cursor was just moved.
		void DrawTableCell(const std::string& text, ImU32 colour, CellAlignment alignment)
		{
			const float room = ImGui::GetContentRegionAvail().x;
			const std::string fitted = FitToWidth(text, room);

			if (alignment == CellAlignment::Right)
			{
				const float textWidth = ImGui::CalcTextSize(fitted.c_str(), fitted.c_str() + fitted.size()).x;
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(room - textWidth, 0.0f));
			}

			ColouredText(colour, fitted);
		}

		std::string PromptTooltipText(const std::string& prompt)
		{
			if (prompt.empty())
			{
				return "(no prompt)";
			}

			if (prompt.size() > kTooltipPromptBytes)
			{
				return SingleLineLabel(prompt, kTooltipPromptBytes);
			}

			return prompt;
		}

		ImGuiCol SplitLineColour()
		{
			if (ImGui::IsItemActive())
			{
				return ImGuiCol_SeparatorActive;
			}

			if (ImGui::IsItemHovered())
			{
				return ImGuiCol_SeparatorHovered;
			}

			return ImGuiCol_Separator;
		}

		std::string StopCellText(const std::string& stopReason)
		{
			return std::string{StopGlyph(stopReason)} + (stopReason.empty() ? "" : " " + SingleLineLabel(stopReason, kStopReasonBytes));
		}
	}

	void RequestsView::Reveal(std::size_t requestIndex, std::uint64_t generation)
	{
		m_Selected = requestIndex;
		m_PendingReveal = requestIndex;
		m_RevealGeneration = generation;
		m_FilterDirty = true;
	}

	void RequestsView::Rebuild(const RequestTimeline& timeline, const Session& session)
	{
		const std::vector<ApiRequest>& requests = timeline.GetRequests();

		const bool isNewGeneration = session.GetGeneration() != m_BuiltGeneration;

		if (isNewGeneration)
		{
			m_Selected.reset();
			m_RebuiltOnly = false;
			m_ToolFilter.clear();

			const bool revealIsFromAnotherGeneration = m_PendingReveal && m_RevealGeneration != session.GetGeneration();

			if (revealIsFromAnotherGeneration)
			{
				m_PendingReveal.reset();
			}

			m_Selected = m_PendingReveal;

			m_ToolNames.clear();
			m_ToolLabels.clear();
			m_AllTools.clear();
			m_Costs.clear();
			m_PriceFrom = 0;

			m_Groups.clear();
			m_RowGroup.clear();
			m_TurnsVersion = kNeverBuilt;
		}

		const bool timelineShrank = requests.size() < m_ToolNames.size();

		if (timelineShrank)
		{
			m_ToolNames.clear();
			m_ToolLabels.clear();
			m_AllTools.clear();
			m_PriceFrom = 0;
		}

		// WARNING: Only the last request can still change within one generation, so the earlier ones are kept
		// rather than rebuilt: a live session appends to the timeline every frame.
		const std::size_t firstPositionToRebuild = m_ToolNames.empty() ? 0 : m_ToolNames.size() - 1;

		m_ToolNames.resize(requests.size());
		m_ToolLabels.resize(requests.size());

		for (std::size_t position = firstPositionToRebuild; position < requests.size(); ++position)
		{
			const ApiRequest& request = requests[position];

			m_ToolNames[position] = ToolNamesUsed(request);
			m_AllTools.insert(m_ToolNames[position].begin(), m_ToolNames[position].end());

			std::string label;
			for (const std::string& name : m_ToolNames[position])
			{
				Core::AppendSeparated(label, ", ", SingleLineLabel(name, kToolNameBytes));
			}
			m_ToolLabels[position] = std::move(label);
		}

		m_PriceFrom = std::min(m_PriceFrom, firstPositionToRebuild);
		m_BuiltVersion = timeline.GetVersion();
		m_BuiltGeneration = session.GetGeneration();
		m_FilterDirty = true;
	}

	void RequestsView::RebuildTurns(const Session& session, const RequestTimeline& timeline)
	{
		m_TurnOf = TurnOfRequests(timeline.GetRequests(), session.GetTurns());

		m_TurnPrompts.clear();
		for (const Turn& turn : session.GetTurns())
		{
			m_TurnPrompts.push_back(turn.Prompt);
		}

		m_TurnsVersion = session.GetVersion();
		m_TurnsTimelineVersion = timeline.GetVersion();

		if (m_GroupByTurn)
		{
			m_FilterDirty = true;
		}
	}

	void RequestsView::PriceRequests(const RequestTimeline& timeline, const Pricing& pricing)
	{
		const std::vector<ApiRequest>& requests = timeline.GetRequests();

		if (pricing.GetEntries() != m_PricedWith)
		{
			m_PricedWith = pricing.GetEntries();
			m_PriceFrom = 0;
			m_FilterDirty = true;
		}

		const bool everyRequestIsPriced = m_PriceFrom >= requests.size() && m_Costs.size() == requests.size();

		if (everyRequestIsPriced)
		{
			return;
		}

		m_Costs.resize(requests.size());

		for (std::size_t position = std::min(m_PriceFrom, requests.size()); position < requests.size(); ++position)
		{
			const ApiRequest& request = requests[position];
			m_Costs[position] = pricing.CostOf(request);
		}

		m_PriceFrom = requests.size();
	}

	bool RequestsView::PassesFilters(const std::vector<ApiRequest>& requests, std::size_t position) const
	{
		if (m_RebuiltOnly && !requests[position].CacheRebuilt)
		{
			return false;
		}

		const std::vector<std::string>& names = m_ToolNames[position];
		return m_ToolFilter.empty() || std::find(names.begin(), names.end(), m_ToolFilter) != names.end();
	}

	void RequestsView::Filter(const RequestTimeline& timeline, const Pricing& pricing)
	{
		const std::vector<ApiRequest>& requests = timeline.GetRequests();
		m_Shown.clear();
		m_Passes.assign(requests.size(), false);

		const bool revealedRequestIsHidden =
			m_PendingReveal && *m_PendingReveal < requests.size() && !PassesFilters(requests, *m_PendingReveal);

		if (revealedRequestIsHidden)
		{
			m_RebuiltOnly = false;
			m_ToolFilter.clear();
		}

		for (std::size_t position = 0; position < requests.size(); ++position)
		{
			if (!PassesFilters(requests, position))
			{
				continue;
			}

			m_Shown.push_back(position);
			m_Passes[position] = true;
		}

		SortRequests(m_Shown, requests, m_SortColumn, m_SortAscending, pricing);
		Regroup(timeline, pricing);
		m_FilterDirty = false;
	}

	void RequestsView::Regroup(const RequestTimeline& timeline, const Pricing& pricing)
	{
		m_Groups.clear();
		m_RowGroup.clear();

		if (!m_GroupByTurn)
		{
			return;
		}

		m_Groups = GroupByTurn(m_Shown, m_TurnOf, timeline.GetRequests(), m_TurnPrompts, m_SortColumn, m_SortAscending,
							   pricing);

		m_Shown.clear();

		for (std::size_t group = 0; group < m_Groups.size(); ++group)
		{
			for (const std::size_t position : m_Groups[group].Positions)
			{
				m_Shown.push_back(position);
				m_RowGroup.push_back(group);
			}
		}
	}

	void RequestsView::DrawFilters(const RequestTimeline& timeline, const Pricing& pricing)
	{
		if (DrawTickBox("Group by turn", &m_GroupByTurn))
		{
			m_FilterDirty = true;
		}

		ImGui::SameLine();

		if (DrawTickBox("Cache rebuilt only", &m_RebuiltOnly))
		{
			m_FilterDirty = true;
		}

		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * kToolFilterWidthInFonts);

		const std::string preview = m_ToolFilter.empty() ? "Any tool" : SingleLineLabel(m_ToolFilter, kToolNameBytes);

		if (ImGui::BeginCombo("##toolFilter", preview.c_str()))
		{
			if (ImGui::Selectable("Any tool", m_ToolFilter.empty()))
			{
				m_ToolFilter.clear();
				m_FilterDirty = true;
			}

			int toolIndex = 0;

			for (const std::string& name : m_AllTools)
			{
				ImGui::PushID(toolIndex++);

				if (ImGui::Selectable(SingleLineLabel(name, kToolNameBytes).c_str(), name == m_ToolFilter))
				{
					m_ToolFilter = name;
					m_FilterDirty = true;
				}

				ImGui::PopID();
			}

			ImGui::EndCombo();
		}

		if (m_FilterDirty)
		{
			Filter(timeline, pricing);
		}

		const std::size_t total = timeline.GetRequests().size();

		ImGui::SameLine();

		std::string count = m_Shown.size() == total ? Core::Counted(total, "request")
													: std::to_string(m_Shown.size()) + " of " + Core::Counted(total, "request");

		if (m_GroupByTurn)
		{
			count += " in " + Core::Counted(m_Groups.size(), "turn");
		}

		ImGui::TextDisabled("%s", count.c_str());
	}

	void RequestsView::DrawTableRow(const ApiRequest& request, std::size_t position, std::size_t row,
									const ConversationContext& context)
	{
		const SessionPalette& palette = context.Palette;
		const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
		const bool hasUsage = request.Usage.Total() > 0;

		ImGui::TableNextRow();
		ImGui::PushID(static_cast<int>(position));

		for (int columnIndex = 0; columnIndex < kColumnCount; ++columnIndex)
		{
			if (!ImGui::TableSetColumnIndex(columnIndex))
			{
				continue;
			}

			const ColumnSpec& columnSpec = kColumns[columnIndex];
			const CellAlignment alignment = AlignmentOf(columnSpec);

			switch (columnSpec.Id)
			{
			case RequestColumn::Index:
			{
				const bool isSelected = m_Selected == position;

				if (ImGui::Selectable("##row", isSelected,
									  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
				{
					m_Selected = position;
				}

				DrawTurnMark(row, palette);

				ImGui::SameLine(0.0f, 0.0f);
				DrawTableCell(std::to_string(request.Index), ink, alignment);
				break;
			}
			case RequestColumn::Time:
			{
				const std::optional<std::chrono::local_seconds> localTime = m_Clock.LocalTime(request.RespondedAt);

				if (!localTime)
				{
					DrawTableCell({}, ink, alignment);
					break;
				}

				DrawTableCell(FormatLocalTime(*localTime, ClockStyle::MonthDayTime), ink, alignment);
				ImGui::SetItemTooltip("%s", FormatLocalTime(*localTime, ClockStyle::DateTimeWeekday).c_str());
				break;
			}
			case RequestColumn::Model:
				DrawTableCell(request.Model.empty() ? std::string{}
													: SingleLineLabel(ModelLabel(request.Model, request.Fast), kModelLabelBytes),
							  ink, alignment);
				break;
			case RequestColumn::Context:
				DrawTableCell(hasUsage ? FormatTokens(request.Context) : std::string{}, ink, alignment);
				break;
			case RequestColumn::Delta:
				DrawTableCell(request.ContextDelta ? SignedTokens(*request.ContextDelta) : std::string{}, ink,
							  alignment);
				break;
			case RequestColumn::CachedShare:
				DrawTableCell(hasUsage && request.Context > 0
								  ? std::to_string(CachedPercent(request.Usage.CacheRead, request.Context)) + "%"
								  : std::string{},
							  ink, alignment);
				break;
			case RequestColumn::Output:
				DrawTableCell(hasUsage ? FormatTokens(request.Usage.Output) : std::string{}, ink, alignment);
				break;
			case RequestColumn::Cost:
				DrawTableCell(hasUsage && position < m_Costs.size() ? FormatCost(m_Costs[position]) : std::string{},
							  ink, alignment);
				break;
			case RequestColumn::Wait:
				DrawTableCell(request.Waited ? WaitText(*request.Waited) : std::string{}, ink, alignment);
				break;
			case RequestColumn::Stop:
				DrawTableCell(StopCellText(request.StopReason), palette.TextDisabled, alignment);
				break;
			case RequestColumn::Tools:
				DrawTableCell(m_ToolLabels[position], palette.TextDisabled, alignment);
				break;
			case RequestColumn::Rebuilt:
				DrawTableCell(request.CacheRebuilt ? std::string{ICON_MDI_CACHED} : std::string{}, palette.Edit,
							  alignment);
				break;
			}
		}

		ImGui::PopID();
	}

	// WARNING: The mark is drawn, not an item, so the row keeps the one item it is clicked with, and it
	// is clipped to the table because this cell's column may not be the leftmost.
	void RequestsView::DrawTurnMark(std::size_t row, const SessionPalette& palette)
	{
		if (!m_GroupByTurn || row >= m_RowGroup.size())
		{
			return;
		}

		const std::size_t group = m_RowGroup[row];
		const TurnSummary& summary = m_Groups[group].Summary;

		const ImGuiTable* table = ImGui::GetCurrentTable();
		const float left = table->WorkRect.Min.x;
		const float width = std::max(std::round(ImGui::GetFontSize() * kTurnMarkWidthInFonts), kTurnMarkMinimumWidth);
		const ImVec2 markTopLeft(left, ImGui::GetItemRectMin().y);
		const ImVec2 markBottomRight(left + width, ImGui::GetItemRectMax().y);

		const ImU32 colour = group % 2 == 0 ? palette.Accent : palette.TextDisabled;
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const bool intersectWithCurrentClip = false;
		drawList->PushClipRect(table->BgClipRect.Min, table->BgClipRect.Max, intersectWithCurrentClip);
		drawList->AddRectFilled(markTopLeft, markBottomRight, colour);
		drawList->PopClipRect();

		const bool clipToWindow = false;
		const bool isOverMark = ImGui::IsMouseHoveringRect(
			ImVec2(markTopLeft.x, markTopLeft.y), ImVec2(markBottomRight.x + width, markBottomRight.y), clipToWindow);

		if (!isOverMark || !ImGui::IsWindowHovered())
		{
			return;
		}

		static const std::string kNoPrompt;
		const std::string& prompt = summary.Turn < m_TurnPrompts.size() ? m_TurnPrompts[summary.Turn] : kNoPrompt;

		const std::string shown = PromptTooltipText(prompt);

		std::string totals = Core::Counted(summary.Count, "request");

		if (summary.HasUsage)
		{
			totals += kMiddleDotSeparator;
			totals += FormatCost(summary.Cost);
			totals += kMiddleDotSeparator;
			totals += FormatTokens(summary.Output);
			totals += " out";
		}

		if (summary.Waited)
		{
			totals += kMiddleDotSeparator;
			totals += WaitText(*summary.Waited);
		}

		if (ImGui::BeginTooltip())
		{
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * kTurnTooltipWrapInFonts);
			ImGui::TextUnformatted(shown.c_str(), shown.c_str() + shown.size());
			ImGui::PopTextWrapPos();
			ImGui::TextDisabled("%s", totals.c_str());
			ImGui::EndTooltip();
		}
	}

	void RequestsView::DrawTable(const RequestTimeline& timeline, const ConversationContext& context)
	{
		constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable |
										   ImGuiTableFlags_Reorderable | ImGuiTableFlags_RowBg |
										   ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
										   ImGuiTableFlags_SizingStretchProp;

		// WARNING: The table id is a constant, not BeginTable's hash of the label under the window, because ImGui
		// saves the columns' widths, order, visibility and sort under it. Changing the string resets every
		// reader's layout.
		static const ImGuiID kTableId = ImHashStr("RequestsView.RequestTable");

		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ListTableCellPadding());

		if (!ImGui::BeginTableEx("##requestTable", kTableId, kColumnCount, kFlags))
		{
			ImGui::PopStyleVar();
			return;
		}

		const float markColumnWidth = ImGui::CalcTextSize(ICON_MDI_CACHED).x + ImGui::GetFontSize();

		for (const ColumnSpec& columnSpec : kColumns)
		{
			const bool isFixedWidth = (columnSpec.Flags & ImGuiTableColumnFlags_WidthFixed) != 0;
			const float columnWidth = isFixedWidth ? markColumnWidth : columnSpec.Weight;
			ImGui::TableSetupColumn(columnSpec.Label, columnSpec.Flags, columnWidth, static_cast<ImGuiID>(columnSpec.Id));
		}

		ImGui::TableSetupScrollFreeze(0, 1);

		// WARNING: The order is compared with this view's own sort as well as read when dirty: the table's
		// sort is shared by every session's tab, and whichever draws first in a frame clears the dirty
		// flag, so another tab would keep an order the header no longer shows.
		if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs(); sortSpecs != nullptr)
		{
			RequestColumn sortColumn = RequestColumn::Index;
			bool sortAscending = true;

			const bool hasKnownColumn =
				sortSpecs->SpecsCount > 0 && sortSpecs->Specs[0].ColumnUserID <= static_cast<ImGuiID>(RequestColumn::Rebuilt);

			if (hasKnownColumn)
			{
				sortColumn = static_cast<RequestColumn>(sortSpecs->Specs[0].ColumnUserID);
				sortAscending = sortSpecs->Specs[0].SortDirection != ImGuiSortDirection_Descending;
			}

			if (sortSpecs->SpecsDirty || sortColumn != m_SortColumn || sortAscending != m_SortAscending)
			{
				m_SortColumn = sortColumn;
				m_SortAscending = sortAscending;
				SortRequests(m_Shown, timeline.GetRequests(), m_SortColumn, m_SortAscending, context.Prices);
				Regroup(timeline, context.Prices);
			}

			sortSpecs->SpecsDirty = false;
		}

		ImGui::TableNextRow(ImGuiTableRowFlags_Headers);

		for (int columnIndex = 0; columnIndex < kColumnCount; ++columnIndex)
		{
			if (!ImGui::TableSetColumnIndex(columnIndex))
			{
				continue;
			}

			ImGui::PushID(columnIndex);
			ImGui::TableHeader(ImGui::TableGetColumnName(columnIndex));

			if (kColumns[columnIndex].Id == RequestColumn::Rebuilt)
			{
				ImGui::SetItemTooltip("Cache rebuilt: at least half the context was written to the cache");
			}

			ImGui::PopID();
		}

		std::optional<int> revealRow;

		if (m_PendingReveal)
		{
			const std::vector<std::size_t>::iterator found = std::find(m_Shown.begin(), m_Shown.end(), *m_PendingReveal);

			if (found != m_Shown.end())
			{
				revealRow = static_cast<int>(found - m_Shown.begin());
			}

			m_PendingReveal.reset();
		}

		const std::vector<ApiRequest>& requests = timeline.GetRequests();
		ImGuiListClipper clipper;
		clipper.Begin(Core::Narrow<int>(m_Shown.size()));

		if (revealRow)
		{
			clipper.IncludeItemByIndex(*revealRow);
		}

		while (clipper.Step())
		{
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
			{
				const std::size_t position = m_Shown[static_cast<std::size_t>(row)];
				DrawTableRow(requests[position], position, static_cast<std::size_t>(row), context);

				if (revealRow && row == *revealRow)
				{
					ImGui::SetScrollHereY(kRevealScrollRatio);
				}
			}
		}

		ImGui::EndTable();
		ImGui::PopStyleVar();
	}

	void RequestsView::DrawDetails(const RequestTimeline& timeline, const ConversationContext& context,
								   Core::EventBus& events)
	{
		const std::vector<ApiRequest>& requests = timeline.GetRequests();

		const ImGuiStyle& style = ImGui::GetStyle();
		const ImVec2 padding = style.WindowPadding;
		const float width = ImGui::GetContentRegionAvail().x;

		if (!m_Selected || *m_Selected >= requests.size())
		{
			return;
		}

		const ApiRequest& request = requests[*m_Selected];

		if (request.Key != m_DetailsKey || request.Index != m_DetailsIndex || request.Sent.size() != m_DetailsSent)
		{
			m_SelectedItem.reset();
			m_RevealItem.reset();
			m_DetailsKey = request.Key;
			m_DetailsIndex = request.Index;
			m_DetailsSent = request.Sent.size();
		}

		if (m_SelectedItem && *m_SelectedItem >= SegmentCount(request))
		{
			m_SelectedItem.reset();
		}

		constexpr Core::IconButtonFit kOnTheTextLine = Core::IconButtonFit::Line;

		ColouredText(ImGui::GetColorU32(ImGuiCol_Text),
					 FitToWidth(RequestLine(request, context.Prices, m_Clock, ClockStyle::DateTime),
								std::max(width - PaneCloseRoom(kOnTheTextLine), 0.0f)));

		if (DrawPaneClose(kOnTheTextLine))
		{
			m_Selected.reset();
			m_SelectedItem.reset();
			m_RevealItem.reset();
			m_DetailsKey.clear();
			m_DetailsIndex = 0;
			m_DetailsSent = 0;
			return;
		}

		const float handle = std::max(std::round(ImGui::GetFontSize() * kSplitHandleWidthInFonts), kSplitHandleMinimumWidth);
		const float room = width + padding.x - handle - 2.0f * style.ItemSpacing.x;
		const float minimum = ImGui::GetFontSize() * kMinimumPaneWidthInFonts;

		// WARNING: The split is stored only once room clears twice the minimum: a dock node still being laid
		// out can hand this a tiny room for a frame, and storing a share of that would stick the split at the
		// minimum. Below that, SplitSideBySide still runs with m_ItemsWidth negative and uses its default.
		if (m_ItemsWidth < 0.0f && room >= minimum * 2.0f)
		{
			m_ItemsWidth = room * kOpeningItemsShare;
		}

		const SideBySideWidths widths = SplitSideBySide(room, m_ItemsWidth, minimum);

		if (std::floor(widths.Left) < IMGUI_WINDOW_HARD_MIN_SIZE || widths.Right < IMGUI_WINDOW_HARD_MIN_SIZE)
		{
			return;
		}

		const float itemsWidth = widths.Left;
		std::optional<std::size_t> hovered;

		ImGui::SetCursorPosX(ImGui::GetCursorPosX() - padding.x);

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding.x, 0.0f));
		const bool itemsOpen = ImGui::BeginChild("##detailItems", ImVec2(itemsWidth, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
		ImGui::PopStyleVar();

		if (itemsOpen)
		{
			const DetailsPick pick =
				DrawRequestDetails(request, context.Palette, m_SelectedItem, std::exchange(m_RevealItem, std::nullopt));

			if (pick.Clicked)
			{
				m_SelectedItem = pick.Clicked;
				m_SentText.ScrollTo(*pick.Clicked);
			}

			hovered = pick.Hovered;
		}

		ImGui::EndChild();
		ImGui::SameLine();

		const ImVec2 handleAt = ImGui::GetCursorScreenPos();
		const float handleHeight = std::max(ImGui::GetContentRegionAvail().y, 1.0f);

		ImGui::InvisibleButton("##detailsSplit", ImVec2(handle, handleHeight));

		if (ImGui::IsItemActive())
		{
			m_ItemsWidth = SplitSideBySide(room, itemsWidth + ImGui::GetIO().MouseDelta.x, minimum).Left;
		}

		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
		{
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
		}

		const ImGuiCol lineColour = SplitLineColour();
		const float lineX = std::floor(handleAt.x + handle * kSplitHandleCentre);
		ImGui::GetWindowDrawList()->AddLine(ImVec2(lineX, handleAt.y), ImVec2(lineX, handleAt.y + handleHeight),
											ImGui::GetColorU32(lineColour));

		ImGui::SameLine();

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding.x, 0.0f));
		const bool textOpen = ImGui::BeginChild("##sentText", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
		ImGui::PopStyleVar();

		if (textOpen)
		{
			if (const std::optional<std::size_t> clicked =
					m_SentText.Draw(request, timeline.GetVersion(), m_SelectedItem, hovered, context.Palette, events))
			{
				m_SelectedItem = clicked;
				m_RevealItem = clicked;
			}
		}

		ImGui::EndChild();
	}

	void RequestsView::Draw(const RequestTimeline& timeline, const Session& session, const ConversationContext& context,
							Core::EventBus& events)
	{
		if (session.GetGeneration() != m_BuiltGeneration || timeline.GetVersion() != m_BuiltVersion)
		{
			Rebuild(timeline, session);
		}

		if (session.GetVersion() != m_TurnsVersion || timeline.GetVersion() != m_TurnsTimelineVersion)
		{
			RebuildTurns(session, timeline);
		}

		PriceRequests(timeline, context.Prices);

		if (timeline.GetRequests().empty())
		{
			Core::EmptyState("No requests yet.");
			return;
		}

		if (m_FilterDirty)
		{
			Filter(timeline, context.Prices);
		}

		const RequestChartInput chartInput{.Timeline = timeline,
										   .Costs = m_Costs,
										   .ToolLabels = m_ToolLabels,
										   .Passes = m_Passes,
										   .Selected = m_Selected,
										   .Generation = m_BuiltGeneration};

		if (const std::optional<std::size_t> clicked = m_Chart.Draw(chartInput, context))
		{
			Reveal(*clicked, m_BuiltGeneration);
		}

		DrawFilters(timeline, context.Prices);

		const bool hasSelection = m_Selected && *m_Selected < timeline.GetRequests().size();

		// WARNING: The table sits in a child of its own with the flags and padding it had before it moved into
		// DrawSplitResults: drawn straight into that unpadded, sideways-scrolling child it would lose its inset
		// and the wheel over it could move both.
		const std::function<void()> drawList = [this, &timeline, &context] {
			if (ImGui::BeginChild("##requestList", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding,
								  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
			{
				if (m_Shown.empty())
				{
					Core::EmptyState("No request matches the filters.");
				}
				else
				{
					DrawTable(timeline, context);
				}
			}

			ImGui::EndChild();
		};

		const std::function<void()> drawDetails = [this, &timeline, &context, &events] { DrawDetails(timeline, context, events); };

		DrawSplitResults(SplitResultsInput{.HasSelection = hasSelection,
										   .ResultsHeight = m_ListHeight,
										   .DrawResults = drawList,
										   .DrawPreview = drawDetails,
										   .ResultsChildId = "##requestResults",
										   .PreviewChildId = "##requestDetails",
										   .SplitterId = "##requestSplit"});
	}
}
