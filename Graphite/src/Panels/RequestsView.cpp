#include "Core/Base/Base.h"

#include "Panels/RequestsView.h"

#include "Panels/RequestRow.h"
#include "Panels/SessionDrawing.h"
#include "Sessions/SessionText.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Graphite {

	namespace {
		// The chart's height, in lines of text: tall enough to tell a cache rebuild from its
		// neighbours, short enough to leave most of the tab to the list.
		constexpr float kChartLines = 12.0f;

		constexpr std::size_t kToolNameBytes = 80;

		ImVec4 Colour(ImU32 colour)
		{
			return ImGui::ColorConvertU32ToFloat4(colour);
		}
	}

	void RequestsView::Reveal(std::size_t requestIndex)
	{
		m_RebuiltOnly = false;
		m_ToolFilter.clear();
		m_Open.insert(requestIndex);

		if (requestIndex < m_Measured.size())
			m_Measured[requestIndex] = false;

		m_PendingReveal = requestIndex;
		m_FilterDirty = true;
	}

	float RequestsView::Estimate(const ApiRequest& request, std::size_t position) const
	{
		const float line = ImGui::GetTextLineHeightWithSpacing();

		if (!m_Open.contains(position))
			return line;

		// The folded line, the three headings, and a line an item — or one saying there is none.
		const float items = static_cast<float>(std::max<std::size_t>(request.Sent.size(), 1) +
											   std::max<std::size_t>(request.Response.size(), 1));
		return line * (4.0f + items);
	}

	void RequestsView::Rebuild(const RequestTimeline& timeline, const Session& session)
	{
		const std::vector<ApiRequest>& requests = timeline.GetRequests();

		// Within one generation the timeline only grows, and the requests already listed keep
		// their places and their measured heights. A new generation is the transcript read again
		// from nothing: what was open, filtered or about to be revealed named other requests.
		// Never having been built is not a new generation, or a Reveal made before the first
		// draw would be lost.
		if (session.GetGeneration() != m_BuiltGeneration)
		{
			if (m_BuiltGeneration != UINT64_MAX)
			{
				m_Open.clear();
				m_RebuiltOnly = false;
				m_ToolFilter.clear();
				m_PendingReveal.reset();
			}

			m_Heights.clear();
			m_Measured.clear();
		}

		const std::size_t kept = std::min(m_Heights.size(), requests.size());

		m_Heights.resize(requests.size());
		m_Measured.resize(requests.size(), false);

		for (std::size_t position = kept; position < requests.size(); ++position)
			m_Heights[position] = Estimate(requests[position], position);

		m_ToolNames.resize(requests.size());

		std::set<std::string> tools;

		for (std::size_t position = 0; position < requests.size(); ++position)
		{
			m_ToolNames[position] = ToolNamesUsed(requests[position]);
			tools.insert(m_ToolNames[position].begin(), m_ToolNames[position].end());
		}

		m_AllTools.assign(tools.begin(), tools.end());

		// Stacked by drawing each bar from the top down, every one over the last: PlotBarGroups
		// takes its colours from the colormap and fixes them when an item is first seen, where
		// this chart's must follow the scheme.
		m_ChartX.resize(requests.size());
		m_ChartTotal.resize(requests.size());
		m_ChartCached.resize(requests.size());
		m_ChartRead.resize(requests.size());

		for (std::size_t position = 0; position < requests.size(); ++position)
		{
			const ApiRequest& request = requests[position];
			const TokenCounts& usage = request.Usage;

			m_ChartX[position] = static_cast<double>(request.Index);
			m_ChartTotal[position] = static_cast<double>(request.Context);
			m_ChartCached[position] = static_cast<double>(usage.CacheRead + usage.CacheWrite5m + usage.CacheWrite1h);
			m_ChartRead[position] = static_cast<double>(usage.CacheRead);
		}

		m_BuiltVersion = timeline.GetVersion();
		m_BuiltGeneration = session.GetGeneration();
		m_FilterDirty = true;
		m_PriceDirty = true;
	}

	void RequestsView::PriceChart(const RequestTimeline& timeline, const Pricing& pricing)
	{
		const std::vector<ApiRequest>& requests = timeline.GetRequests();
		m_ChartCost.resize(requests.size());

		// A request with no price has no point on the line, rather than a point at zero.
		for (std::size_t position = 0; position < requests.size(); ++position)
		{
			const ApiRequest& request = requests[position];
			const CostResult cost = pricing.CostOf({{UsageKey{request.Model, request.Fast}, request.Usage}});

			m_ChartCost[position] = cost.IsKnown() ? cost.Dollars : std::numeric_limits<double>::quiet_NaN();
		}

		m_PricedWith = pricing.GetEntries();
		m_PriceDirty = false;
	}

	void RequestsView::Filter(const RequestTimeline& timeline)
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

		m_FilterDirty = false;
		m_OffsetsDirty = true;
	}

	void RequestsView::DrawChart(const SessionPalette& palette)
	{
		const int count = static_cast<int>(m_ChartX.size());

		if (!ImPlot::BeginPlot("##requestsChart", ImVec2(-1.0f, ImGui::GetFontSize() * kChartLines)))
			return;

		ImPlot::SetupAxis(ImAxis_X1, "Request", ImPlotAxisFlags_AutoFit);
		ImPlot::SetupAxis(ImAxis_Y1, "Tokens", ImPlotAxisFlags_AutoFit);
		ImPlot::SetupAxis(ImAxis_Y2, "Dollars", ImPlotAxisFlags_AuxDefault | ImPlotAxisFlags_AutoFit);

		// Hiding a series would uncover the one drawn under it in the wrong colour.
		ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_NoButtons);

		constexpr double kBarWidth = 0.67;

		ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
		ImPlot::SetNextFillStyle(Colour(palette.Run));
		ImPlot::PlotBars("Uncached input", m_ChartX.data(), m_ChartTotal.data(), count, kBarWidth);
		ImPlot::SetNextFillStyle(Colour(palette.Edit));
		ImPlot::PlotBars("Cache write", m_ChartX.data(), m_ChartCached.data(), count, kBarWidth);
		ImPlot::SetNextFillStyle(Colour(palette.Web));
		ImPlot::PlotBars("Cache read", m_ChartX.data(), m_ChartRead.data(), count, kBarWidth);

		ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
		ImPlot::SetNextLineStyle(Colour(palette.Accent));
		ImPlot::PlotLine("Cost", m_ChartX.data(), m_ChartCost.data(), count, ImPlotLineFlags_SkipNaN);

		// A click anywhere over a request's column opens it in the list: the bars are too thin
		// to be hit exactly once there are a few hundred of them.
		if (count > 0 && ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			const double x = ImPlot::GetPlotMousePos(ImAxis_X1, ImAxis_Y1).x;
			const double nearest = std::clamp(std::round(x), 1.0, static_cast<double>(count));

			Reveal(static_cast<std::size_t>(nearest) - 1);
		}

		ImPlot::EndPlot();
	}

	void RequestsView::DrawFilters(const RequestTimeline& timeline)
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

			for (std::size_t index = 0; index < m_AllTools.size(); ++index)
			{
				const std::string& name = m_AllTools[index];

				// By place, so two names that read the same once cut to a label stay apart.
				ImGui::PushID(static_cast<int>(index));

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
			Filter(timeline);

		const std::size_t total = timeline.GetRequests().size();
		const char* noun = total == 1 ? "request" : "requests";

		ImGui::SameLine();

		if (m_Shown.size() == total)
			ImGui::TextDisabled("%zu %s", total, noun);
		else
			ImGui::TextDisabled("%zu of %zu %s", m_Shown.size(), total, noun);
	}

	void RequestsView::DrawRow(const ApiRequest& request, std::size_t position, const ConversationContext& context)
	{
		ImGui::PushID(static_cast<int>(position));

		const bool open = m_Open.contains(position);
		const float lineHeight = ImGui::GetTextLineHeight();
		const ImVec2 start = ImGui::GetCursorScreenPos();
		const float right = start.x + ImGui::GetContentRegionAvail().x;

		if (ImGui::Selectable("##request", false, ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, lineHeight)))
		{
			if (open)
				m_Open.erase(position);
			else
				m_Open.insert(position);

			m_Measured[position] = false;
			m_OffsetsDirty = true;
		}

		ImVec2 at = start;
		DrawPiece(at, ImGui::GetColorU32(ImGuiCol_Text),
				  std::string{open ? ICON_MDI_CHEVRON_DOWN : ICON_MDI_CHEVRON_RIGHT} + " ");
		DrawRequestLine(at, right, request, context.Prices, context.Palette, m_Clock);

		if (open)
		{
			ImGui::Indent();
			DrawRequestDetails(request, context.Palette);
			ImGui::Unindent();
		}

		ImGui::PopID();
	}

	void RequestsView::DrawList(const RequestTimeline& timeline, const ConversationContext& context)
	{
		if (!ImGui::BeginChild("##requests", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_None))
		{
			ImGui::EndChild();
			return;
		}

		const std::vector<ApiRequest>& requests = timeline.GetRequests();

		if (m_OffsetsDirty)
		{
			m_Offsets.assign(m_Shown.size() + 1, 0.0f);

			for (std::size_t row = 0; row < m_Shown.size(); ++row)
				m_Offsets[row + 1] = m_Offsets[row] + m_Heights[m_Shown[row]];

			m_OffsetsDirty = false;
		}

		// Scrolled to on the next frame, when the child applies its scroll target. The row's top
		// is exact: every row above it that was never drawn is a folded one, one line tall.
		if (m_PendingReveal)
		{
			const auto found = std::lower_bound(m_Shown.begin(), m_Shown.end(), *m_PendingReveal);

			if (found != m_Shown.end() && *found == *m_PendingReveal)
				ImGui::SetScrollY(m_Offsets[static_cast<std::size_t>(found - m_Shown.begin())]);

			m_PendingReveal.reset();
		}

		const float top = ImGui::GetScrollY();
		const float bottom = top + ImGui::GetWindowHeight();

		// Rows overlapping the visible band, plus one either side so a row half in view is drawn.
		const auto first = std::upper_bound(m_Offsets.begin(), m_Offsets.end(), top);
		std::size_t begin = first == m_Offsets.begin() ? 0 : static_cast<std::size_t>(first - m_Offsets.begin()) - 1;
		begin = begin > 0 ? begin - 1 : 0;

		const float startY = ImGui::GetCursorPosY();

		for (std::size_t row = begin; row < m_Shown.size() && m_Offsets[row] < bottom + m_Heights[m_Shown[row]]; ++row)
		{
			const std::size_t position = m_Shown[row];

			ImGui::SetCursorPosY(startY + m_Offsets[row]);
			const float before = ImGui::GetCursorPosY();

			DrawRow(requests[position], position, context);

			const float taken = ImGui::GetCursorPosY() - before;

			if (!m_Measured[position] || taken != m_Heights[position])
			{
				m_Heights[position] = taken;
				m_Measured[position] = true;
				m_OffsetsDirty = true;
			}
		}

		// The child's scroll range is the whole list, drawn or not. The Dummy closes the layout:
		// a SetCursorPos with no item after it is an assertion failure in ImGui.
		ImGui::SetCursorPosY(startY + m_Offsets.back());
		ImGui::Dummy(ImVec2(0.0f, 0.0f));

		ImGui::EndChild();
	}

	void RequestsView::Draw(const RequestTimeline& timeline, const Session& session, const ConversationContext& context)
	{
		if (session.GetGeneration() != m_BuiltGeneration || timeline.GetVersion() != m_BuiltVersion)
			Rebuild(timeline, session);

		if (m_PriceDirty || context.Prices.GetEntries() != m_PricedWith)
			PriceChart(timeline, context.Prices);

		if (timeline.GetRequests().empty())
		{
			ImGui::TextDisabled("No requests yet.");
			return;
		}

		DrawChart(context.Palette);
		DrawFilters(timeline);
		DrawList(timeline, context);
	}
}
