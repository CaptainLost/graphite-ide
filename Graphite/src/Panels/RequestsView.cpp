#include "Core/Base/Base.h"

#include "Panels/RequestsView.h"

#include "Panels/RequestRow.h"
#include "Panels/SessionDrawing.h"
#include "Sessions/SessionText.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace Graphite {

	namespace {
		// The chart's height, in lines of text: tall enough to tell a cache rebuild from its
		// neighbours, short enough to leave most of the tab to the table.
		constexpr float kChartLines = 12.0f;

		constexpr std::size_t kToolNameBytes = 80;

		// The chart's series, which name its legend entries too.
		constexpr const char* kSeries[] = {"Uncached input", "Cache write", "Cache read", "Cost"};

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

		ImVec4 Colour(ImU32 colour)
		{
			return ImGui::ColorConvertU32ToFloat4(colour);
		}

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
		m_RebuiltOnly = false;
		m_ToolFilter.clear();
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
		}

		// Within one generation the timeline only grows, and of the requests already built only
		// the last can still change: the rest of its response is still being written. The ones
		// before it are kept as they are. A timeline that shrank all the same is built again.
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
		m_ChartX.resize(requests.size());
		m_ChartTotal.resize(requests.size());
		m_ChartCached.resize(requests.size());
		m_ChartRead.resize(requests.size());

		for (std::size_t position = from; position < requests.size(); ++position)
		{
			const ApiRequest& request = requests[position];

			m_ToolNames[position] = ToolNamesUsed(request);
			m_AllTools.insert(m_ToolNames[position].begin(), m_ToolNames[position].end());

			std::string label;
			for (const std::string& name : m_ToolNames[position])
				label += (label.empty() ? "" : ", ") + SingleLineLabel(name, kToolNameBytes);
			m_ToolLabels[position] = std::move(label);

			// Stacked by drawing each bar from the top down, every one over the last:
			// PlotBarGroups takes its colours from the colormap and fixes them when an item is
			// first seen, where this chart's must follow the scheme.
			const TokenCounts& usage = request.Usage;
			m_ChartX[position] = static_cast<double>(request.Index);
			m_ChartTotal[position] = static_cast<double>(request.Context);
			m_ChartCached[position] = static_cast<double>(usage.CacheRead + usage.CacheWrite5m + usage.CacheWrite1h);
			m_ChartRead[position] = static_cast<double>(usage.CacheRead);
		}

		m_PriceFrom = std::min(m_PriceFrom, from);
		m_BuiltVersion = timeline.GetVersion();
		m_BuiltGeneration = session.GetGeneration();
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
		m_ChartCost.resize(requests.size());

		// A request with no price has no point on the line, rather than a point at zero.
		for (std::size_t position = std::min(m_PriceFrom, requests.size()); position < requests.size(); ++position)
		{
			const ApiRequest& request = requests[position];
			m_Costs[position] = pricing.CostOf({{UsageKey{request.Model, request.Fast}, request.Usage}});
			m_ChartCost[position] =
				m_Costs[position].IsKnown() ? m_Costs[position].Dollars : std::numeric_limits<double>::quiet_NaN();
		}

		m_PriceFrom = requests.size();
	}

	void RequestsView::Filter(const RequestTimeline& timeline, const Pricing& pricing)
	{
		const std::vector<ApiRequest>& requests = timeline.GetRequests();
		m_Shown.clear();

		for (std::size_t position = 0; position < requests.size(); ++position)
		{
			if (m_RebuiltOnly && !requests[position].CacheRebuilt)
				continue;

			const std::vector<std::string>& names = m_ToolNames[position];

			if (!m_ToolFilter.empty() && std::find(names.begin(), names.end(), m_ToolFilter) == names.end())
				continue;

			m_Shown.push_back(position);
		}

		SortRequests(m_Shown, requests, m_SortColumn, m_SortAscending, pricing);
		m_FilterDirty = false;
	}

	void RequestsView::DrawChart(const SessionPalette& palette)
	{
		const int count = static_cast<int>(m_ChartX.size());

		if (!ImPlot::BeginPlot("##requestsChart", ImVec2(-1.0f, ImGui::GetFontSize() * kChartLines)))
			return;

		// The requests axis is set once and then left to the reader to pan and zoom; the value
		// axes follow whatever part of it is in view.
		ImPlot::SetupAxis(ImAxis_X1, "Request", ImPlotAxisFlags_None);
		ImPlot::SetupAxisLimits(ImAxis_X1, 0.5, static_cast<double>(count) + 0.5, ImPlotCond_Once);
		ImPlot::SetupAxis(ImAxis_Y1, "Tokens", ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit);
		ImPlot::SetupAxis(ImAxis_Y2, "Dollars",
						  ImPlotAxisFlags_AuxDefault | ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit);

		// Hiding a series would uncover the one drawn under it in the wrong colour.
		ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_NoButtons);

		constexpr double kBarWidth = 0.67;

		ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
		ImPlot::SetNextFillStyle(Colour(palette.Run));
		ImPlot::PlotBars(kSeries[0], m_ChartX.data(), m_ChartTotal.data(), count, kBarWidth);
		ImPlot::SetNextFillStyle(Colour(palette.Edit));
		ImPlot::PlotBars(kSeries[1], m_ChartX.data(), m_ChartCached.data(), count, kBarWidth);
		ImPlot::SetNextFillStyle(Colour(palette.Web));
		ImPlot::PlotBars(kSeries[2], m_ChartX.data(), m_ChartRead.data(), count, kBarWidth);

		ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
		ImPlot::SetNextLineStyle(Colour(palette.Accent));
		ImPlot::PlotLine(kSeries[3], m_ChartX.data(), m_ChartCost.data(), count, ImPlotLineFlags_SkipNaN);

		// A click anywhere over a request's column selects it in the table: the bars are too
		// thin to be hit exactly once there are a few hundred of them. Taken on release and only
		// without a drag, which pans the chart; and not over the legend, which sits on the bars.
		const bool overLegend = std::any_of(std::begin(kSeries), std::end(kSeries),
											[](const char* series) { return ImPlot::IsLegendEntryHovered(series); });
		const bool clicked = ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
							 !ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left);

		if (count > 0 && clicked && ImPlot::IsPlotHovered() && !overLegend)
		{
			const double x = ImPlot::GetPlotMousePos(ImAxis_X1, ImAxis_Y1).x;
			const double nearest = std::clamp(std::round(x), 1.0, static_cast<double>(count));

			Reveal(static_cast<std::size_t>(nearest) - 1, m_BuiltGeneration);
		}

		ImPlot::EndPlot();
	}

	void RequestsView::DrawFilters(const RequestTimeline& timeline, const Pricing& pricing)
	{
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

		if (m_Shown.size() == total)
			ImGui::TextDisabled("%zu %s", total, noun);
		else
			ImGui::TextDisabled("%zu of %zu %s", m_Shown.size(), total, noun);
	}

	void RequestsView::DrawTableRow(const ApiRequest& request, std::size_t position, const ConversationContext& context)
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

				ImGui::SameLine(0.0f, 0.0f);
				Cell(std::to_string(request.Index), ink, spec.Numeric);
				break;
			}
			case RequestColumn::Time: Cell(m_Clock.HoursMinutesSeconds(request.RespondedAt), ink, false); break;
			case RequestColumn::Model:
				Cell(request.Model.empty() ? std::string{}
										   : SingleLineLabel(ShortModelName(request.Model), 80) + (request.Fast ? " fast" : ""),
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
			case RequestColumn::Tools: Cell(m_ToolLabels[position], palette.TextDisabled, false); break;
			case RequestColumn::Rebuilt:
				Cell(request.CacheRebuilt ? std::string{ICON_MDI_CACHED} : std::string{}, palette.Edit, false);
				break;
			}
		}

		ImGui::PopID();
	}

	void RequestsView::DrawTable(const RequestTimeline& timeline, const ConversationContext& context)
	{
		constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable |
										   ImGuiTableFlags_Reorderable | ImGuiTableFlags_RowBg |
										   ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
										   ImGuiTableFlags_SizingStretchProp;

		// Fills the child it is in; the table scrolls itself, under its frozen header.
		if (!ImGui::BeginTable("##requestTable", kColumnCount, kFlags))
			return;

		// The mark's column is as wide as the mark and the sort arrow its header may show.
		const float markWidth = ImGui::CalcTextSize(ICON_MDI_CACHED).x + ImGui::GetFontSize();

		for (const ColumnSpec& spec : kColumns)
		{
			const float width = (spec.Flags & ImGuiTableColumnFlags_WidthFixed) != 0 ? markWidth : spec.Weight;
			ImGui::TableSetupColumn(spec.Label, spec.Flags, width, static_cast<ImGuiID>(spec.Id));
		}

		ImGui::TableSetupScrollFreeze(0, 1);

		if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs != nullptr && specs->SpecsDirty)
		{
			m_SortColumn = RequestColumn::Index;
			m_SortAscending = true;

			if (specs->SpecsCount > 0 && specs->Specs[0].ColumnUserID <= static_cast<ImGuiID>(RequestColumn::Rebuilt))
			{
				m_SortColumn = static_cast<RequestColumn>(specs->Specs[0].ColumnUserID);
				m_SortAscending = specs->Specs[0].SortDirection != ImGuiSortDirection_Descending;
			}

			SortRequests(m_Shown, timeline.GetRequests(), m_SortColumn, m_SortAscending, context.Prices);
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

		// The row to scroll to, in the table's order. The filters were cleared by Reveal, so it
		// is there unless the timeline no longer has it.
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
				DrawTableRow(requests[position], position, context);

				if (revealRow && row == *revealRow)
					ImGui::SetScrollHereY(0.5f);
			}
		}

		ImGui::EndTable();
	}

	void RequestsView::DrawDetails(const RequestTimeline& timeline, const ConversationContext& context)
	{
		const std::vector<ApiRequest>& requests = timeline.GetRequests();

		if (!m_Selected || *m_Selected >= requests.size())
		{
			ImGui::TextDisabled("Select a request to see what it sent and returned.");
			return;
		}

		const ApiRequest& request = requests[*m_Selected];

		ColouredText(ImGui::GetColorU32(ImGuiCol_Text),
					 Fitted(RequestLine(request, context.Prices, m_Clock), ImGui::GetContentRegionAvail().x));
		DrawRequestDetails(request, context.Palette);
	}

	void RequestsView::Draw(const RequestTimeline& timeline, const Session& session, const ConversationContext& context)
	{
		if (session.GetGeneration() != m_BuiltGeneration || timeline.GetVersion() != m_BuiltVersion)
			Rebuild(timeline, session);

		PriceRequests(timeline, context.Prices);

		if (timeline.GetRequests().empty())
		{
			ImGui::TextDisabled("No requests yet.");
			return;
		}

		DrawChart(context.Palette);
		DrawFilters(timeline, context.Prices);

		// The table above, the selected request's details below, and between them a handle
		// that moves the line between the two. Each side keeps a few lines however far it is
		// dragged.
		const ImGuiStyle& style = ImGui::GetStyle();
		const float handle = std::max(std::round(ImGui::GetFontSize() * 0.4f), 4.0f);
		const float room = ImGui::GetContentRegionAvail().y - handle - 2.0f * style.ItemSpacing.y;
		const float minimum = ImGui::GetFrameHeightWithSpacing() * 3.0f;

		if (m_ListHeight < 0.0f)
			m_ListHeight = room * 0.55f;

		const float listHeight = std::max(std::min(m_ListHeight, room - minimum), minimum);

		if (ImGui::BeginChild("##requestList", ImVec2(0.0f, listHeight), ImGuiChildFlags_AlwaysUseWindowPadding,
							  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
		{
			if (m_Shown.empty())
				ImGui::TextDisabled("No request matches the filters.");
			else
				DrawTable(timeline, context);
		}

		ImGui::EndChild();

		const ImVec2 handleAt = ImGui::GetCursorScreenPos();
		const float handleWidth = std::max(ImGui::GetContentRegionAvail().x, 1.0f);

		ImGui::InvisibleButton("##requestSplit", ImVec2(handleWidth, handle));

		if (ImGui::IsItemActive())
			m_ListHeight = std::max(std::min(listHeight + ImGui::GetIO().MouseDelta.y, room - minimum), minimum);

		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

		const ImGuiCol lineColour = ImGui::IsItemActive()	 ? ImGuiCol_SeparatorActive
									: ImGui::IsItemHovered() ? ImGuiCol_SeparatorHovered
															 : ImGuiCol_Separator;
		const float lineY = std::floor(handleAt.y + handle * 0.5f);
		ImGui::GetWindowDrawList()->AddLine(ImVec2(handleAt.x, lineY), ImVec2(handleAt.x + handleWidth, lineY),
											ImGui::GetColorU32(lineColour));

		if (ImGui::BeginChild("##requestDetails", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding))
			DrawDetails(timeline, context);

		ImGui::EndChild();
	}
}
