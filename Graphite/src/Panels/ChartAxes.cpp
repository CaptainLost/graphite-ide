#include "Core/Base/Base.h"

#include "Panels/ChartAxes.h"

#include <format>
#include <imgui.h>
#include <implot.h>
#include <implot_internal.h>

#include <algorithm>
#include <cmath>

namespace Graphite {

	void FollowingAxis::Setup(double low, double high)
	{
		if (!m_FitPending || m_ViewMoved)
		{
			return;
		}

		ImPlot::SetupAxisLimits(ImAxis_X1, low, high, ImPlotCond_Always);
		m_FitPending = false;
	}

	void FollowingAxis::Track()
	{
		// ImPlot works out the legend's hover as it draws it in EndPlot, so this is the last
		// frame's, which is where the mouse was when a button under it was pressed.
		if (ImPlot::GetCurrentPlot()->Items.Legend.Hovered)
		{
			return;
		}

		if (!ImPlot::IsPlotHovered() && !ImPlot::IsAxisHovered(ImAxis_X1))
		{
			return;
		}

		// A pan with the left button, a box zoom with the right, or the wheel.
		if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) || ImGui::IsMouseDragging(ImGuiMouseButton_Right) ||
			ImGui::GetIO().MouseWheel != 0.0f)
		{
			m_ViewMoved = true;
		}

		if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			m_ViewMoved = false;
		}
	}

	void SetupCountedAxis()
	{
		ImPlot::SetupAxisFormat(
			ImAxis_X1,
			[](double value, char* buffer, int size, void*) -> int {
				// A tick between two whole numbers keeps its grid line and loses its label.
				if (std::abs(value - std::round(value)) > 1e-9)
				{
					buffer[0] = '\0';
					return 0;
				}

				// ImPlot hands over the buffer, so the text is written into it, cut to fit.
				const auto written = std::format_to_n(buffer, size - 1, "{:.0f}", value);
				*written.out = '\0';
				return static_cast<int>(written.out - buffer);
			});
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
		{
			list->AddText(ImVec2(plot.FrameRect.Min.x + style.PlotPadding.x, y), colour, left);
		}

		if (right != nullptr)
		{
			const float width = ImGui::CalcTextSize(right).x;
			list->AddText(ImVec2(plot.FrameRect.Max.x - style.PlotPadding.x - width, y), colour, right);
		}
	}

	void DrawColumnBand(double x, double slot, ImU32 accent)
	{
		// How strongly the band is drawn, and its narrowest, in font sizes.
		constexpr float kBandAlpha = 0.18f;
		constexpr float kMinBandEms = 0.5f;

		float left = ImPlot::PlotToPixels(x - slot * 0.5, 0.0, ImAxis_X1, ImAxis_Y1).x;
		float right = ImPlot::PlotToPixels(x + slot * 0.5, 0.0, ImAxis_X1, ImAxis_Y1).x;

		// Widened symmetrically about the column's centre, which left and right already are
		// equidistant from.
		const float widen = std::max(0.0f, ImGui::GetFontSize() * kMinBandEms - (right - left)) * 0.5f;
		left -= widen;
		right += widen;

		ImVec4 colour = ImGui::ColorConvertU32ToFloat4(accent);
		colour.w *= kBandAlpha;

		const ImVec2 plotPos = ImPlot::GetPlotPos();
		const ImVec2 plotSize = ImPlot::GetPlotSize();

		ImPlot::PushPlotClipRect();
		ImPlot::GetPlotDrawList()->AddRectFilled(ImVec2(left, plotPos.y), ImVec2(right, plotPos.y + plotSize.y),
												 ImGui::GetColorU32(colour));
		ImPlot::PopPlotClipRect();
	}
}
