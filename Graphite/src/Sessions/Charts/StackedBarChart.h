#pragma once

#include <imgui.h>
#include <implot.h>

#include <span>
#include <string>

namespace Graphite {

	inline constexpr float kChartLines = 16.0f;

	inline constexpr float kTooltipEms = 36.0f;

	// WARNING: ImPlot's ApplyFit pads each end of the range it fits by half of this, so an axis held at zero
	// gets only the half on top.
	inline constexpr float kFitPadding = 0.12f;

	ImVec4 PlotColour(ImU32 colour);

	bool SeriesShown(const char* label);

	bool BeginBarChart(const char* title, const char* id);
	void EndBarChart();

	struct BarChartValues
	{
		const char* BarCaption = nullptr;
		ImPlotScale BarScale = ImPlotScale_Linear;

		const char* LineCaption = nullptr;

		bool LineFromZero = false;
	};

	void FinishBarChartSetup(const BarChartValues& values);

	struct BarLayer
	{
		const char* Id = nullptr;
		ImVec4 Colour;
		std::span<const double> Tops;
	};

	void PlotStackedBars(std::span<const BarLayer> layers, std::span<const double> barPositions, double width,
						 ImPlotBarsFlags flags = ImPlotBarsFlags_None);

	bool ColumnClicked();

	bool ChartPanning();

	struct SwatchRow
	{
		ImU32 Colour = 0;
		const char* Label = nullptr;
		std::string Number;
		std::string Note;
		ImU32 NoteColour = 0;
	};

	void DrawSwatchRows(std::span<const SwatchRow> rows, ImU32 ink);
}
