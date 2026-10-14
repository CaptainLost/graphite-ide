#include "Sessions/Charts/ChartAxes.h"

#include "Core/Base/Base.h"
#include "Core/ImGui/ImGuiColors.h"

#include <imgui.h>
#include <implot.h>
#include <implot_internal.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace Graphite {

	namespace {
		constexpr double kWholeNumberTolerance = 1e-9;
		constexpr float kBandAlpha = 0.18f;
		constexpr float kMinBandEms = 0.5f;
		constexpr float kHalfOfSpan = 0.5f;
		constexpr double kHalfOfSlot = 0.5;
		constexpr double kBandBaselineY = 0.0;

		int FormatWholeNumberTick(double value, char* buffer, int bufferSize, void*)
		{
			if (std::abs(value - std::round(value)) > kWholeNumberTolerance)
			{
				buffer[0] = '\0';
				return 0;
			}

			const std::format_to_n_result<char*> formatted = std::format_to_n(buffer, bufferSize - 1, "{:.0f}", value);
			*formatted.out = '\0';
			return static_cast<int>(formatted.out - buffer);
		}
	}

	void FollowingAxis::Setup(double low, double high)
	{
		if (!m_FitPending || m_ViewMoved)
		{
			return;
		}

		// WARNING: ImPlotCond_Always applies for this one frame only, because an axis set that way
		// takes no input.
		ImPlot::SetupAxisLimits(ImAxis_X1, low, high, ImPlotCond_Always);
		m_FitPending = false;
	}

	void FollowingAxis::Track()
	{
		// WARNING: ImPlot works out the legend's hover while drawing it in EndPlot, so this is the
		// previous frame's, which is where the mouse was when a button under it was pressed.
		if (ImPlot::GetCurrentPlot()->Items.Legend.Hovered)
		{
			return;
		}

		const bool overPlotOrXAxis = ImPlot::IsPlotHovered() || ImPlot::IsAxisHovered(ImAxis_X1);

		if (!overPlotOrXAxis)
		{
			return;
		}

		const bool panned = ImGui::IsMouseDragging(ImGuiMouseButton_Left);
		const bool boxZoomed = ImGui::IsMouseDragging(ImGuiMouseButton_Right);
		const bool wheelTurned = ImGui::GetIO().MouseWheel != 0.0f;

		if (panned || boxZoomed || wheelTurned)
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
		ImPlot::SetupAxisFormat(ImAxis_X1, FormatWholeNumberTick);
	}

	void DrawAxisCaptions(const char* left, const char* right)
	{
		const ImPlotPlot& plot = *ImPlot::GetCurrentPlot();
		const ImPlotStyle& style = ImPlot::GetStyle();
		const ImU32 captionColour = ImPlot::GetStyleColorU32(ImPlotCol_AxisText);
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		const float y = plot.PlotRect.Min.y - ImGui::GetTextLineHeight() - style.LabelPadding.y;

		if (left != nullptr)
		{
			drawList->AddText(ImVec2(plot.FrameRect.Min.x + style.PlotPadding.x, y), captionColour, left);
		}

		if (right != nullptr)
		{
			const float rightWidth = ImGui::CalcTextSize(right).x;
			drawList->AddText(ImVec2(plot.FrameRect.Max.x - style.PlotPadding.x - rightWidth, y), captionColour, right);
		}
	}

	void DrawColumnBand(double x, double slot, ImU32 accent)
	{
		const double halfSlot = slot * kHalfOfSlot;
		float left = ImPlot::PlotToPixels(x - halfSlot, kBandBaselineY, ImAxis_X1, ImAxis_Y1).x;
		float right = ImPlot::PlotToPixels(x + halfSlot, kBandBaselineY, ImAxis_X1, ImAxis_Y1).x;

		const float shortfall = std::max(0.0f, ImGui::GetFontSize() * kMinBandEms - (right - left));
		const float widenEachSide = shortfall * kHalfOfSpan;
		left -= widenEachSide;
		right += widenEachSide;

		const ImVec4 bandColour = Core::WithAlphaScaled(ImGui::ColorConvertU32ToFloat4(accent), kBandAlpha);

		const ImVec2 plotPosition = ImPlot::GetPlotPos();
		const ImVec2 plotSize = ImPlot::GetPlotSize();

		ImPlot::PushPlotClipRect();
		ImPlot::GetPlotDrawList()->AddRectFilled(ImVec2(left, plotPosition.y),
												 ImVec2(right, plotPosition.y + plotSize.y),
												 ImGui::GetColorU32(bandColour));
		ImPlot::PopPlotClipRect();
	}
}
