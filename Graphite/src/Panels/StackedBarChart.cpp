#include "Panels/StackedBarChart.h"

#include "Core/Base/Base.h"

#include "Core/Base/Narrow.h"

#include "Panels/ChartAxes.h"
#include "Panels/SessionDrawing.h"

#include <imgui.h>
#include <implot.h>
#include <implot_internal.h>

#include <algorithm>
#include <cmath>

namespace Graphite {

	ImVec4 PlotColour(ImU32 colour)
	{
		return ImGui::ColorConvertU32ToFloat4(colour);
	}

	bool SeriesShown(const char* label)
	{
		const ImPlotItem* item = ImPlot::GetItem(label);
		return item == nullptr || item->Show;
	}

	bool BeginBarChart(const char* title, const char* id)
	{
		ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0.0f, kFitPadding));

		const std::string plotId = std::string(title) + "###" + id;

		if (ImPlot::BeginPlot(plotId.c_str(), ImVec2(-1.0f, ImGui::GetFontSize() * kChartLines)))
		{
			return true;
		}

		ImPlot::PopStyleVar();
		return false;
	}

	void EndBarChart()
	{
		ImPlot::EndPlot();
		ImPlot::PopStyleVar();
	}

	void FinishBarChartSetup(const BarChartValues& values)
	{
		// RangeFit fits each value axis to the bars in view, so a zoomed-in stretch is not
		// flattened by the rest. ApplyFit pads both ends of the range it fits, and with the
		// floor free that lifts the bars off the axis; held at zero here, once, so they still
		// stand on it.
		ImPlot::SetupAxis(ImAxis_Y1, nullptr, ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_LockMin);
		ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 1.0, ImPlotCond_Once);

		if (values.BarScale != ImPlotScale_Linear)
		{
			ImPlot::SetupAxisScale(ImAxis_Y1, values.BarScale);
		}

		if (values.LineCaption != nullptr)
		{
			// A line fitted to its own first and last values would make any growth look like it
			// filled the chart; one that stands on zero does not.
			ImPlotAxisFlags flags = ImPlotAxisFlags_AuxDefault | ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit;

			if (values.LineFromZero)
			{
				flags |= ImPlotAxisFlags_LockMin;
			}

			ImPlot::SetupAxis(ImAxis_Y2, nullptr, flags);

			if (values.LineFromZero)
			{
				ImPlot::SetupAxisLimits(ImAxis_Y2, 0.0, 1.0, ImPlotCond_Once);
			}
		}

		ImPlot::SetupLegend(ImPlotLocation_North, ImPlotLegendFlags_Outside | ImPlotLegendFlags_Horizontal);
		ImPlot::SetupFinish();

		DrawAxisCaptions(values.BarCaption, values.LineCaption);
	}

	void PlotStackedBars(std::span<const BarLayer> layers, std::span<const double> xs, double width,
						 ImPlotBarsFlags flags)
	{
		ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);

		const int count = Core::Narrow<int>(xs.size());

		for (std::size_t layer = layers.size(); layer-- > 0;)
		{
			const BarLayer& bars = layers[layer];

			// ImPlot reads `count` tops from the pointer it is handed, whatever was allocated.
			CORE_ASSERT(bars.Tops.size() >= xs.size(), "a bar layer has fewer tops than there are bars");

			ImPlot::SetNextFillStyle(bars.Colour);
			ImPlot::SetNextLineStyle(bars.Colour);
			ImPlot::PlotBars(bars.Id, xs.data(), bars.Tops.data(), count, width, flags);
		}
	}

	bool ColumnClicked()
	{
		return ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left);
	}

	bool ChartPanning()
	{
		return ImGui::IsMouseDragging(ImGuiMouseButton_Left);
	}

	void DrawSwatchRows(std::span<const SwatchRow> rows, ImU32 ink)
	{
		float labelWidth = 0.0f;
		float numberWidth = 0.0f;

		for (const SwatchRow& row : rows)
		{
			labelWidth = std::max(labelWidth, ImGui::CalcTextSize(row.Label).x);
			numberWidth = std::max(numberWidth, ImGui::CalcTextSize(row.Number.c_str()).x);
		}

		const float lineHeight = ImGui::GetTextLineHeight();
		const float space = ImGui::GetStyle().ItemSpacing.x;
		const float indent = ImGui::GetFontSize();
		const float swatch = std::round(lineHeight * 0.55f);

		for (const SwatchRow& row : rows)
		{
			const ImVec2 start = ImGui::GetCursorScreenPos();
			const float swatchTop = std::floor(start.y + (lineHeight - swatch) * 0.5f);

			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(start.x + indent, swatchTop),
													  ImVec2(start.x + indent + swatch, swatchTop + swatch), row.Colour);

			ImVec2 at(start.x + indent + swatch + space, start.y);
			const float numbersRight = at.x + labelWidth + 2.0f * space + numberWidth;

			DrawPiece(at, ink, row.Label);

			at.x = numbersRight - ImGui::CalcTextSize(row.Number.c_str()).x;
			DrawPiece(at, ink, row.Number);

			float right = numbersRight;

			if (!row.Note.empty())
			{
				at.x = numbersRight + 2.0f * space;
				DrawPiece(at, row.NoteColour, row.Note);
				right = std::max(at.x, numbersRight);
			}

			// The pieces go through the draw list; this is the line's one item, as wide as they
			// reach, so the tooltip is sized to them.
			ImGui::Dummy(ImVec2(right - start.x, lineHeight));
		}
	}
}
