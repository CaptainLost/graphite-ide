#include "Core/Base/Base.h"
#include "Core/ImGui/EmptyState.h"

#include "Panels/RequestsView.h"

#include "Panels/RequestRow.h"
#include "Panels/SessionDrawing.h"
#include "Sessions/SentTextLayout.h"
#include "Sessions/SessionText.h"
#include "Sessions/ToolFacts.h"
#include "UI/SideBySide.h"
#include "UI/SplitResults.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iterator>
#include <utility>

namespace Graphite {

	namespace {
		constexpr std::size_t kToolNameBytes = 80;

		// The table's columns, in the order they start in. The sort column travels as the
		// column's user id, so a reader's reordering or hiding never changes what a sort means.
		struct ColumnSpec
		{
			const char* Label;
			RequestColumn Id;
			ImGuiTableColumnFlags Flags;
			float Weight;
			bool Numeric;
		};

		// "Δ" is U+0394. The last column is the rebuilt mark alone: its header is the mark too.
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

		// One cell's text, cut to the column so it never runs into the next, and pushed to the
		// right edge for a number so a column of them lines up.
		void Cell(const std::string& text, ImU32 colour, bool alignRight)
		{
			const float room = ImGui::GetContentRegionAvail().x;
			const std::string fitted = Fitted(text, room);

			if (alignRight)
			{
				const float width = ImGui::CalcTextSize(fitted.c_str(), fitted.c_str() + fitted.size()).x;
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(room - width, 0.0f));
			}

			// An item on every path, the empty text included: the cursor was just moved.
			ColouredText(colour, fitted);
		}
	}

	void RequestsView::Reveal(std::size_t requestIndex, std::uint64_t generation)
	{
		// The filters stay unless they hide this request, which Filter finds out once it knows
		// what the request used.
		m_Selected = requestIndex;
		m_PendingReveal = requestIndex;
		m_RevealGeneration = generation;
		m_FilterDirty = true;
	}

	void RequestsView::Rebuild(const RequestTimeline& timeline, const Session& session)
	{
		const std::vector<ApiRequest>& requests = timeline.GetRequests();

		// A new generation is the transcript read again from nothing: what was selected or
		// filtered named other requests. A reveal asked for in this very generation — the
		// conversation, drawn first, may ask for one on the frame the restart lands — is the one
		// thing kept.
		if (session.GetGeneration() != m_BuiltGeneration)
		{
			m_Selected.reset();
			m_RebuiltOnly = false;
			m_ToolFilter.clear();

			if (m_PendingReveal && m_RevealGeneration != session.GetGeneration())
				m_PendingReveal.reset();

			m_Selected = m_PendingReveal;

			m_ToolNames.clear();
			m_ToolLabels.clear();
			m_AllTools.clear();
			m_Costs.clear();
			m_PriceFrom = 0;

			m_Groups.clear();
			m_RowGroup.clear();
			m_TurnsVersion = UINT64_MAX;
		}

		// Within one generation the timeline only grows. Of what is kept here per request — the
		// tools it used — only the last request's can still change: the rest of its response is
		// still being written. An earlier request can change too, when a prompt snapshot puts the
		// system prompt and the tools at the head of the first one, but those are neither tool
		// results nor usage, and nothing kept here reads them. So the ones before the last are
		// kept as they are. A timeline that shrank all the same is built again.
		if (requests.size() < m_ToolNames.size())
		{
			m_ToolNames.clear();
			m_ToolLabels.clear();
			m_AllTools.clear();
			m_PriceFrom = 0;
		}

		const std::size_t from = m_ToolNames.empty() ? 0 : m_ToolNames.size() - 1;

		m_ToolNames.resize(requests.size());
		m_ToolLabels.resize(requests.size());

		for (std::size_t position = from; position < requests.size(); ++position)
		{
			const ApiRequest& request = requests[position];

			m_ToolNames[position] = ToolNamesUsed(request);
			m_AllTools.insert(m_ToolNames[position].begin(), m_ToolNames[position].end());

			std::string label;
			for (const std::string& name : m_ToolNames[position])
				label += (label.empty() ? "" : ", ") + SingleLineLabel(name, kToolNameBytes);
			m_ToolLabels[position] = std::move(label);
		}

		m_PriceFrom = std::min(m_PriceFrom, from);
		m_BuiltVersion = timeline.GetVersion();
		m_BuiltGeneration = session.GetGeneration();
		m_FilterDirty = true;
	}

	void RequestsView::RebuildTurns(const Session& session, const RequestTimeline& timeline)
	{
		m_TurnOf = TurnOfRequests(timeline.GetRequests(), session.GetTurns());

		m_TurnPrompts.clear();
		for (const Turn& turn : session.GetTurns())
			m_TurnPrompts.push_back(turn.Prompt);

		m_TurnsVersion = session.GetVersion();
		m_TurnsTimelineVersion = timeline.GetVersion();

		// Only the grouped table reads the turns; the flat one has nothing to redo.
		if (m_GroupByTurn)
			m_FilterDirty = true;
	}

	void RequestsView::PriceRequests(const RequestTimeline& timeline, const Pricing& pricing)
	{
		const std::vector<ApiRequest>& requests = timeline.GetRequests();

		// An edited price changes every request's cost, and with a table sorted by cost, the
		// order too.
		if (pricing.GetEntries() != m_PricedWith)
		{
			m_PricedWith = pricing.GetEntries();
			m_PriceFrom = 0;
			m_FilterDirty = true;
		}

		if (m_PriceFrom >= requests.size() && m_Costs.size() == requests.size())
			return;

		m_Costs.resize(requests.size());

		for (std::size_t position = std::min(m_PriceFrom, requests.size()); position < requests.size(); ++position)
		{
			const ApiRequest& request = requests[position];
			m_Costs[position] = pricing.CostOf(request);
		}

		m_PriceFrom = requests.size();
	}

	void RequestsView::Filter(const RequestTimeline& timeline, const Pricing& pricing)
	{
		const std::vector<ApiRequest>& requests = timeline.GetRequests();
		m_Shown.clear();
		m_Passes.assign(requests.size(), false);

		const auto passes = [this, &requests](std::size_t position) {
			if (m_RebuiltOnly && !requests[position].CacheRebuilt)
				return false;

			const std::vector<std::string>& names = m_ToolNames[position];
			return m_ToolFilter.empty() || std::find(names.begin(), names.end(), m_ToolFilter) != names.end();
		};

		// A request asked to be shown has to be in the list, so filters that would hide it are
		// dropped. Filters it passes are the reader's and stay.
		if (m_PendingReveal && *m_PendingReveal < requests.size() && !passes(*m_PendingReveal))
		{
			m_RebuiltOnly = false;
			m_ToolFilter.clear();
		}

		for (std::size_t position = 0; position < requests.size(); ++position)
		{
			if (!passes(position))
				continue;

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
			return;

		m_Groups = GroupByTurn(m_Shown, m_TurnOf, timeline.GetRequests(), m_TurnPrompts, m_SortColumn, m_SortAscending,
							   pricing);

		// m_Shown in the groups' order: each turn's requests together, in the order GroupByTurn
		// keeps them.
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
		if (ImGui::Checkbox("Group by turn", &m_GroupByTurn))
			m_FilterDirty = true;

		ImGui::SameLine();

		if (ImGui::Checkbox("Cache rebuilt only", &m_RebuiltOnly))
			m_FilterDirty = true;

		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);

		const std::string preview = m_ToolFilter.empty() ? "Any tool" : SingleLineLabel(m_ToolFilter, kToolNameBytes);

		if (ImGui::BeginCombo("##toolFilter", preview.c_str()))
		{
			if (ImGui::Selectable("Any tool", m_ToolFilter.empty()))
			{
				m_ToolFilter.clear();
				m_FilterDirty = true;
			}

			int index = 0;

			for (const std::string& name : m_AllTools)
			{
				// By place, so two names that read the same once cut to a label stay apart.
				ImGui::PushID(index++);

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
			Filter(timeline, pricing);

		const std::size_t total = timeline.GetRequests().size();
		const char* noun = total == 1 ? "request" : "requests";

		ImGui::SameLine();

		std::string count = m_Shown.size() == total
								? std::to_string(total) + " " + noun
								: std::to_string(m_Shown.size()) + " of " + std::to_string(total) + " " + noun;

		if (m_GroupByTurn)
			count += " in " + std::to_string(m_Groups.size()) + (m_Groups.size() == 1 ? " turn" : " turns");

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

		// Cell by cell in the order the columns were set up, which is not the order a reader
		// may have dragged them into; a hidden one is skipped.
		for (int column = 0; column < kColumnCount; ++column)
		{
			if (!ImGui::TableSetColumnIndex(column))
				continue;

			const ColumnSpec& spec = kColumns[column];

			switch (spec.Id)
			{
			case RequestColumn::Index:
			{
				// The row's one item that takes a click, across every column. `#` cannot be
				// hidden, so the row always has it.
				if (ImGui::Selectable("##row", m_Selected == position,
									  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
					m_Selected = position;

				DrawTurnMark(row, palette);

				ImGui::SameLine(0.0f, 0.0f);
				Cell(std::to_string(request.Index), ink, spec.Numeric);
				break;
			}
			case RequestColumn::Time:
			{
				// The day on every row, not only where it changes: a column that switches format as
				// the sort order changes reads as two different columns, and the width is there.
				const auto local = m_Clock.LocalTime(request.RespondedAt);

				if (!local)
				{
					Cell({}, ink, false);
					break;
				}

				Cell(FormatLocalTime(*local, ClockStyle::MonthDayTime), ink, false);
				ImGui::SetItemTooltip("%s", FormatLocalTime(*local, ClockStyle::DateTimeWeekday).c_str());
				break;
			}
			case RequestColumn::Model:
				Cell(request.Model.empty() ? std::string{}
										   : SingleLineLabel(ModelLabel(request.Model, request.Fast), 80),
					 ink, false);
				break;
			case RequestColumn::Context:
				Cell(hasUsage ? FormatTokens(request.Context) : std::string{}, ink, true);
				break;
			case RequestColumn::Delta:
				Cell(request.ContextDelta ? SignedTokens(*request.ContextDelta) : std::string{}, ink, true);
				break;
			case RequestColumn::CachedShare:
				Cell(hasUsage && request.Context > 0
						 ? std::to_string(CachedPercent(request.Usage.CacheRead, request.Context)) + "%"
						 : std::string{},
					 ink, true);
				break;
			case RequestColumn::Output:
				Cell(hasUsage ? FormatTokens(request.Usage.Output) : std::string{}, ink, true);
				break;
			case RequestColumn::Cost:
				Cell(hasUsage && position < m_Costs.size() ? FormatCost(m_Costs[position]) : std::string{}, ink, true);
				break;
			case RequestColumn::Wait:
				Cell(request.Waited ? WaitText(*request.Waited) : std::string{}, ink, true);
				break;
			case RequestColumn::Stop:
				Cell(std::string{StopGlyph(request.StopReason)} +
						 (request.StopReason.empty() ? "" : " " + SingleLineLabel(request.StopReason, 60)),
					 palette.TextDisabled, false);
				break;
			case RequestColumn::Tools:
				Cell(m_ToolLabels[position], palette.TextDisabled, false);
				break;
			case RequestColumn::Rebuilt:
				Cell(request.CacheRebuilt ? std::string{ICON_MDI_CACHED} : std::string{}, palette.Edit, false);
				break;
			}
		}

		ImGui::PopID();
	}

	void RequestsView::DrawTurnMark(std::size_t row, const SessionPalette& palette)
	{
		if (!m_GroupByTurn || row >= m_RowGroup.size())
			return;

		const std::size_t group = m_RowGroup[row];
		const TurnSummary& summary = m_Groups[group].Summary;

		// Down the row's left edge, the row's full height: the turns alternate between the two
		// colours, so where one turn gives way to the next the colour changes. The edge is the
		// table's, not this cell's — the reader may have dragged another column to the left of
		// #. The height is the row's Selectable, just drawn, which reaches from one row to the
		// next: the table's own row bounds are not final until the row ends. Drawn, not an item,
		// so the row keeps the one item it clicks with, and clipped to the table rather than to
		// this cell's column, which may not be the leftmost.
		const ImGuiTable* table = ImGui::GetCurrentTable();
		const float left = table->WorkRect.Min.x;
		const float width = std::max(std::round(ImGui::GetFontSize() * 0.2f), 2.0f);
		const ImVec2 min(left, ImGui::GetItemRectMin().y);
		const ImVec2 max(left + width, ImGui::GetItemRectMax().y);

		const ImU32 colour = group % 2 == 0 ? palette.Accent : palette.TextDisabled;
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->PushClipRect(table->BgClipRect.Min, table->BgClipRect.Max, false);
		drawList->AddRectFilled(min, max, colour);
		drawList->PopClipRect();

		// What the turn was, over its mark: the prompt, and what its requests came to.
		if (!ImGui::IsMouseHoveringRect(ImVec2(min.x, min.y), ImVec2(max.x + width, max.y), false) || !ImGui::IsWindowHovered())
			return;

		static const std::string kNoPrompt;
		const std::string& prompt = summary.Turn < m_TurnPrompts.size() ? m_TurnPrompts[summary.Turn] : kNoPrompt;

		constexpr std::size_t kTooltipBytes = 2000;
		const std::string shown = prompt.empty()				  ? std::string{"(no prompt)"}
								  : prompt.size() > kTooltipBytes ? SingleLineLabel(prompt, kTooltipBytes)
																  : prompt;

		std::string totals = std::to_string(summary.Count) + (summary.Count == 1 ? " request" : " requests");

		if (summary.HasUsage)
			totals += " \xc2\xb7 " + FormatCost(summary.Cost) + " \xc2\xb7 " + FormatTokens(summary.Output) + " out";

		if (summary.Waited)
			totals += " \xc2\xb7 " + WaitText(*summary.Waited);

		if (ImGui::BeginTooltip())
		{
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
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

		// Fills the child it is in; the table scrolls itself, under its frozen header.
		//
		// Its id is a constant rather than BeginTable's hash of the label under the window it is
		// drawn in. ImGui saves a table's column widths, order, visibility and sort under that
		// id, so an id taken from the window path lost the reader's columns whenever the
		// windows around the table were nested differently, and gave each session's tab a
		// layout of its own that a newly opened session started without. With the constant, the
		// saved layout outlives changes to what the table is drawn in — it resets once more,
		// the time this id changed to the constant — and is one layout for every session's
		// tab. Everything the table keys by id below it — its rows and cells, its header's
		// context menu — derives from this id, not from the window's. Its scrolling child is
		// the exception: ImGui names that after the enclosing window, so it and its scroll
		// stay per session tab, which is what is wanted.
		static const ImGuiID kTableId = ImHashStr("RequestsView.RequestTable");

		if (!ImGui::BeginTableEx("##requestTable", kTableId, kColumnCount, kFlags))
			return;

		// The mark's column is as wide as the mark and the sort arrow its header may show.
		const float markWidth = ImGui::CalcTextSize(ICON_MDI_CACHED).x + ImGui::GetFontSize();

		for (const ColumnSpec& spec : kColumns)
		{
			const float width = (spec.Flags & ImGuiTableColumnFlags_WidthFixed) != 0 ? markWidth : spec.Weight;
			ImGui::TableSetupColumn(spec.Label, spec.Flags, width, static_cast<ImGuiID>(spec.Id));
		}

		ImGui::TableSetupScrollFreeze(0, 1);

		// Compared with this view's own order as well as read when dirty: the table, and so its
		// sort, is shared by every session's tab, and whichever tab draws it first in a frame
		// clears the flag — another tab visible beside it, or one opened after the sort was
		// set, would otherwise keep an order the header no longer shows.
		if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs != nullptr)
		{
			RequestColumn column = RequestColumn::Index;
			bool ascending = true;

			if (specs->SpecsCount > 0 && specs->Specs[0].ColumnUserID <= static_cast<ImGuiID>(RequestColumn::Rebuilt))
			{
				column = static_cast<RequestColumn>(specs->Specs[0].ColumnUserID);
				ascending = specs->Specs[0].SortDirection != ImGuiSortDirection_Descending;
			}

			if (specs->SpecsDirty || column != m_SortColumn || ascending != m_SortAscending)
			{
				m_SortColumn = column;
				m_SortAscending = ascending;
				SortRequests(m_Shown, timeline.GetRequests(), m_SortColumn, m_SortAscending, context.Prices);
				Regroup(timeline, context.Prices);
			}

			specs->SpecsDirty = false;
		}

		// The header row by hand rather than TableHeadersRow, for the mark's column to say what
		// the mark means.
		ImGui::TableNextRow(ImGuiTableRowFlags_Headers);

		for (int column = 0; column < kColumnCount; ++column)
		{
			if (!ImGui::TableSetColumnIndex(column))
				continue;

			ImGui::PushID(column);
			ImGui::TableHeader(ImGui::TableGetColumnName(column));

			if (kColumns[column].Id == RequestColumn::Rebuilt)
				ImGui::SetItemTooltip("Cache rebuilt: at least half the context was written to the cache");

			ImGui::PopID();
		}

		// The row to scroll to, in the table's order. Filter dropped whichever filters would hide
		// the request, so it is there unless the timeline no longer has it.
		std::optional<int> revealRow;

		if (m_PendingReveal)
		{
			const auto found = std::find(m_Shown.begin(), m_Shown.end(), *m_PendingReveal);

			if (found != m_Shown.end())
				revealRow = static_cast<int>(found - m_Shown.begin());

			m_PendingReveal.reset();
		}

		const std::vector<ApiRequest>& requests = timeline.GetRequests();
		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(m_Shown.size()));

		// Drawn whether or not it is in view, so it has a place to scroll to.
		if (revealRow)
			clipper.IncludeItemByIndex(*revealRow);

		while (clipper.Step())
		{
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
			{
				const std::size_t position = m_Shown[static_cast<std::size_t>(row)];
				DrawTableRow(requests[position], position, static_cast<std::size_t>(row), context);

				if (revealRow && row == *revealRow)
					ImGui::SetScrollHereY(0.5f);
			}
		}

		ImGui::EndTable();
	}

	void RequestsView::DrawDetails(const RequestTimeline& timeline, const ConversationContext& context)
	{
		const std::vector<ApiRequest>& requests = timeline.GetRequests();

		// Drawn as DrawSplitResults's preview, which Draw asks for only with a request selected
		// and which pads all four sides itself: the line above the two panes needs no nudge in
		// from the pane's edge to sit under the table's.
		const ImGuiStyle& style = ImGui::GetStyle();
		const ImVec2 padding = style.WindowPadding;
		const float width = ImGui::GetContentRegionAvail().x;

		if (!m_Selected || *m_Selected >= requests.size())
			return;

		const ApiRequest& request = requests[*m_Selected];

		// Another request, or new items put ahead of the ones this one had — a prompt snapshot
		// arriving under the first request — and the selected position names some other item.
		// Items added after them, as a response is written, move nothing.
		if (request.Key != m_DetailsKey || request.Index != m_DetailsIndex || request.Sent.size() != m_DetailsSent)
		{
			m_SelectedItem.reset();
			m_RevealItem.reset();
			m_DetailsKey = request.Key;
			m_DetailsIndex = request.Index;
			m_DetailsSent = request.Sent.size();
		}

		// A request whose items shrank under a live transcript's rewrite has no such item.
		if (m_SelectedItem && *m_SelectedItem >= SegmentCount(request))
			m_SelectedItem.reset();

		// The line stops short of the close DrawPaneClose puts in the pane's corner, with an
		// item's spacing between them, so a long line is cut rather than drawn under it.
		const float closeWidth = ImGui::CalcTextSize(ICON_MDI_CLOSE).x + style.FramePadding.x * 2.0f;
		ColouredText(ImGui::GetColorU32(ImGuiCol_Text),
					 Fitted(RequestLine(request, context.Prices, m_Clock, ClockStyle::DateTime),
							std::max(width - closeWidth - style.ItemSpacing.x, 0.0f)));

		// Last on the line, the way the app's other detail panes end theirs: the close places
		// itself at the right corner. Dismissing drops the request and what was picked in it, so
		// the next frame draws the table alone; the rest of this one is not drawn, since it would
		// be drawn for a request no longer selected.
		if (DrawPaneClose())
		{
			m_Selected.reset();
			m_SelectedItem.reset();
			m_RevealItem.reset();
			m_DetailsKey.clear();
			m_DetailsIndex = 0;
			m_DetailsSent = 0;
			return;
		}

		// The items on the left, their text on the right, and between them a handle that moves
		// the line between the two. Each side keeps some width however far it is dragged.
		//
		// Each pane pads its sides, so what it draws at its right keeps a gap before its
		// scrollbar, as the table's cells do; the rows run to the top and the bottom. The pane
		// this is drawn in pads its sides too, and the items' rows would then start two paddings
		// in, a padding right of the line above them — so the items pane starts a padding left
		// of the cursor, on the pane's inner edge, and its own padding brings its rows back under
		// the line. The text fills the rest to the content region's right edge and no further:
		// that puts its scrollbar in line with the table's, which sits inside the same padding,
		// and nothing reaches past the content region, where the pane's horizontal scrollbar
		// would start counting it.
		//
		// So the two panes share `room`: the width from the items pane's left edge to the
		// content region's right edge, less the handle and the item spacing either side of it.
		// SplitSideBySide keeps the sum of the two within it at any width — on a pane too narrow
		// for both minimums each side gets half — and the text pane, filling the rest, is then
		// what is left of it (ImGui cuts the items pane to whole pixels, which only gives the
		// text the fraction). Only below ImGui's smallest child window, which it would widen to
		// that size and so past the edge, are the two not drawn at all: at that width there is
		// nothing to read in them anyway.
		const float handle = std::max(std::round(ImGui::GetFontSize() * 0.4f), 4.0f);
		const float room = width + padding.x - handle - 2.0f * style.ItemSpacing.x;
		const float minimum = ImGui::GetFontSize() * 12.0f;

		// Waits for room to clear twice the minimum before locking in 40% of it: a dock node
		// still being laid out can hand this a room that is transiently tiny for a frame, and
		// storing 40% of that would stick the split at the minimum instead of the width it is
		// meant to open at. Below that threshold SplitSideBySide is still called, with
		// m_ItemsWidth left negative, and falls back to its own default for the frame.
		if (m_ItemsWidth < 0.0f && room >= minimum * 2.0f)
			m_ItemsWidth = room * 0.4f;

		const SideBySideWidths widths = SplitSideBySide(room, m_ItemsWidth, minimum);

		if (std::floor(widths.Left) < IMGUI_WINDOW_HARD_MIN_SIZE || widths.Right < IMGUI_WINDOW_HARD_MIN_SIZE)
			return;

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
			m_ItemsWidth = SplitSideBySide(room, itemsWidth + ImGui::GetIO().MouseDelta.x, minimum).Left;

		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

		const ImGuiCol lineColour = ImGui::IsItemActive()	 ? ImGuiCol_SeparatorActive
									: ImGui::IsItemHovered() ? ImGuiCol_SeparatorHovered
															 : ImGuiCol_Separator;
		const float lineX = std::floor(handleAt.x + handle * 0.5f);
		ImGui::GetWindowDrawList()->AddLine(ImVec2(lineX, handleAt.y), ImVec2(lineX, handleAt.y + handleHeight),
											ImGui::GetColorU32(lineColour));

		ImGui::SameLine();

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding.x, 0.0f));
		const bool textOpen = ImGui::BeginChild("##sentText", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
		ImGui::PopStyleVar();

		if (textOpen)
		{
			if (const std::optional<std::size_t> clicked =
					m_SentText.Draw(request, timeline.GetVersion(), m_SelectedItem, hovered, context.Palette))
			{
				m_SelectedItem = clicked;
				m_RevealItem = clicked;
			}
		}

		ImGui::EndChild();
	}

	void RequestsView::Draw(const RequestTimeline& timeline, const Session& session, const ConversationContext& context)
	{
		if (session.GetGeneration() != m_BuiltGeneration || timeline.GetVersion() != m_BuiltVersion)
			Rebuild(timeline, session);

		if (session.GetVersion() != m_TurnsVersion || timeline.GetVersion() != m_TurnsTimelineVersion)
			RebuildTurns(session, timeline);

		PriceRequests(timeline, context.Prices);

		if (timeline.GetRequests().empty())
		{
			Core::EmptyState("No requests yet.");
			return;
		}

		// Filtered before the chart is drawn, so it dims what the table leaves out from the
		// frame a request arrives. A filter the reader changes below is taken on the next frame.
		if (m_FilterDirty)
			Filter(timeline, context.Prices);

		const RequestChartInput chartInput{.Timeline = timeline,
										   .Costs = m_Costs,
										   .ToolLabels = m_ToolLabels,
										   .Passes = m_Passes,
										   .Selected = m_Selected,
										   .Generation = m_BuiltGeneration};

		if (const std::optional<std::size_t> clicked = m_Chart.Draw(chartInput, context))
			Reveal(*clicked, m_BuiltGeneration);

		DrawFilters(timeline, context.Prices);

		// The table fills the tab until a request is picked, and then shares it with that
		// request's details below a divider the reader drags — the layout of the app's other
		// lists with a detail pane, whose close in the corner hands the tab back to the table.
		// A selection that names no request any more, after the timeline was read again, shows
		// no pane rather than an empty one.
		const bool hasSelection = m_Selected && *m_Selected < timeline.GetRequests().size();

		// Inside a child of its own, with the flags and the padding the table had before it
		// was drawn in DrawSplitResults: that child is unpadded and scrolls sideways, and the
		// table is sized to fill whatever holds it and scrolls itself. Drawn straight into it,
		// the table would lose the padding that sets it in from the chart and the filters above,
		// and the wheel over it could move that child as well as the table.
		const std::function<void()> drawList = [this, &timeline, &context] {
			if (ImGui::BeginChild("##requestList", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding,
								  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
			{
				if (m_Shown.empty())
					Core::EmptyState("No request matches the filters.");
				else
					DrawTable(timeline, context);
			}

			ImGui::EndChild();
		};

		const std::function<void()> drawDetails = [this, &timeline, &context] { DrawDetails(timeline, context); };

		DrawSplitResults(SplitResultsInput{.HasSelection = hasSelection,
										   .ResultsHeight = m_ListHeight,
										   .DrawResults = drawList,
										   .DrawPreview = drawDetails,
										   .ResultsChildId = "##requestResults",
										   .PreviewChildId = "##requestDetails",
										   .SplitterId = "##requestSplit"});
	}
}
