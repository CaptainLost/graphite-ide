#include "Sessions/Charts/StackedBarChart.h"

#include "Core/Base/Base.h"

#include "Core/Base/Narrow.h"

#include "Sessions/Charts/ChartAxes.h"
#include "Sessions/SessionDrawing.h"

#include <imgui.h>
#include <implot.h>
#include <implot_internal.h>

#include <algorithm>
#include <cmath>

namespace Graphite {

	namespace {
		constexpr float kSwatchLineShare = 0.55f;
		constexpr float kColumnGapSpacings = 2.0f;
	}

	ImVec4 PlotColour(ImU32 colour)
	{
		return ImGui::ColorConvertU32ToFloat4(colour);
	}

	// WARNING: Call this only after SetupFinish, which pushes the id the legend items are hashed under.
	bool SeriesShown(const char* label)
	{
		const ImPlotItem* item = ImPlot::GetItem(label);
		return item == nullptr || item->Show;
	}

	bool BeginBarChart(const char* title, const char* id)
	{
		// WARNING: ImPlot reads the fit padding in EndPlot, so it stays pushed until EndBarChart.
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
		// WARNING: ApplyFit pads both ends of the range it fits, so a free floor would lift the bars off the
		// axis; the limits are held at zero once so the bars stand on it.
		ImPlot::SetupAxis(ImAxis_Y1, nullptr, ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_LockMin);
		ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 1.0, ImPlotCond_Once);

		if (values.BarScale != ImPlotScale_Linear)
		{
			ImPlot::SetupAxisScale(ImAxis_Y1, values.BarScale);
		}

		if (values.LineCaption != nullptr)
		{
			ImPlotAxisFlags lineAxisFlags = ImPlotAxisFlags_AuxDefault | ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit;

			if (values.LineFromZero)
			{
				lineAxisFlags |= ImPlotAxisFlags_LockMin;
			}

			ImPlot::SetupAxis(ImAxis_Y2, nullptr, lineAxisFlags);

			if (values.LineFromZero)
			{
				ImPlot::SetupAxisLimits(ImAxis_Y2, 0.0, 1.0, ImPlotCond_Once);
			}
		}

		ImPlot::SetupLegend(ImPlotLocation_North, ImPlotLegendFlags_Outside | ImPlotLegendFlags_Horizontal);
		// WARNING: SetupFinish is called by hand so the legend's show state can be read before anything is
		// plotted.
		ImPlot::SetupFinish();

		DrawAxisCaptions(values.BarCaption, values.LineCaption);
	}

	// WARNING: The layers are drawn top down, each over the last, instead of through PlotBarGroups, which
	// fixes its colours from the colormap when an item is first seen where these must follow the scheme.
	void PlotStackedBars(std::span<const BarLayer> layers, std::span<const double> barPositions, double width,
						 ImPlotBarsFlags flags)
	{
		ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);

		const int barCount = Core::Narrow<int>(barPositions.size());

		for (std::size_t layerIndex = layers.size(); layerIndex-- > 0;)
		{
			const BarLayer& barLayer = layers[layerIndex];

			// WARNING: ImPlot reads barCount tops from the pointer it is handed, whatever was allocated.
			CORE_ASSERT(barLayer.Tops.size() >= barPositions.size(), "a bar layer has fewer tops than there are bars");

			ImPlot::SetNextFillStyle(barLayer.Colour);
			ImPlot::SetNextLineStyle(barLayer.Colour);
			ImPlot::PlotBars(barLayer.Id, barPositions.data(), barLayer.Tops.data(), barCount, width, flags);
		}
	}

	// WARNING: The drag distance outlives the release that ends a drag, which is what makes it the test for a
	// click.
	bool ColumnClicked()
	{
		return ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left);
	}

	// WARNING: Asked of the button held now; the drag distance ColumnClicked reads is kept until the next
	// click and would hide the tooltip after any drag anywhere.
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
		const float itemSpacing = ImGui::GetStyle().ItemSpacing.x;
		const float indent = ImGui::GetFontSize();
		const float swatchSize = std::round(lineHeight * kSwatchLineShare);
		const float columnGap = kColumnGapSpacings * itemSpacing;

		for (const SwatchRow& row : rows)
		{
			const ImVec2 start = ImGui::GetCursorScreenPos();
			const float swatchTop = std::floor(start.y + (lineHeight - swatchSize) * 0.5f);

			ImGui::GetWindowDrawList()->AddRectFilled(
				ImVec2(start.x + indent, swatchTop), ImVec2(start.x + indent + swatchSize, swatchTop + swatchSize),
				row.Colour);

			ImVec2 pieceOrigin(start.x + indent + swatchSize + itemSpacing, start.y);
			const float numbersRight = pieceOrigin.x + labelWidth + columnGap + numberWidth;

			DrawPiece(pieceOrigin, ink, row.Label);

			pieceOrigin.x = numbersRight - ImGui::CalcTextSize(row.Number.c_str()).x;
			DrawPiece(pieceOrigin, ink, row.Number);

			float lineRight = numbersRight;

			if (!row.Note.empty())
			{
				pieceOrigin.x = numbersRight + columnGap;
				DrawPiece(pieceOrigin, row.NoteColour, row.Note);
				lineRight = std::max(pieceOrigin.x, numbersRight);
			}

			ImGui::Dummy(ImVec2(lineRight - start.x, lineHeight));
		}
	}
}
