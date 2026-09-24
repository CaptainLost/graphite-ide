#include "Core/Base/Base.h"

#include "Panels/ChartAxes.h"

#include <imgui.h>
#include <implot.h>
#include <implot_internal.h>

namespace Graphite {

	void FollowingAxis::Setup(double low, double high)
	{
		if (!m_FitPending || m_ViewMoved)
			return;

		ImPlot::SetupAxisLimits(ImAxis_X1, low, high, ImPlotCond_Always);
		m_FitPending = false;
	}

	void FollowingAxis::Track()
	{
		// ImPlot works out the legend's hover as it draws it in EndPlot, so this is the last
		// frame's, which is where the mouse was when a button under it was pressed.
		if (ImPlot::GetCurrentPlot()->Items.Legend.Hovered)
			return;

		if (!ImPlot::IsPlotHovered() && !ImPlot::IsAxisHovered(ImAxis_X1))
			return;

		// A pan with the left button, a box zoom with the right, or the wheel.
		if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) || ImGui::IsMouseDragging(ImGuiMouseButton_Right) ||
			ImGui::GetIO().MouseWheel != 0.0f)
			m_ViewMoved = true;

		if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			m_ViewMoved = false;
	}

	void DrawAxisCaptions(const char* left, const char* right)
	{
		const ImPlotPlot& plot = *ImPlot::GetCurrentPlot();
		const ImPlotStyle& style = ImPlot::GetStyle();
		const ImU32 colour = ImPlot::GetStyleColorU32(ImPlotCol_AxisText);
		ImDrawList* list = ImGui::GetWindowDrawList();

		// Over the tick labels of the axis each names, on the line above the plot area.
		const float y = plot.PlotRect.Min.y - ImGui::GetTextLineHeight() - style.LabelPadding.y;

		if (left != nullptr)
			list->AddText(ImVec2(plot.FrameRect.Min.x + style.PlotPadding.x, y), colour, left);

		if (right != nullptr)
		{
			const float width = ImGui::CalcTextSize(right).x;
			list->AddText(ImVec2(plot.FrameRect.Max.x - style.PlotPadding.x - width, y), colour, right);
		}
	}
}
