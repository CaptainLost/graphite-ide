#pragma once

#include <imgui.h>
#include <implot.h>

#include <span>
#include <string>

namespace Graphite {

	// What the session tabs' stacked-bar charts share — the Cost tab's cost per turn or per
	// request, and the context or time per request above the Requests table — so the two are
	// drawn, sized and clicked alike. Each chart keeps its own data, its x axis and its tooltip's
	// content.

	// A chart's height, in lines of text: tall enough to tell a cache rebuild from its
	// neighbours, short enough to leave most of the tab to the table under it. One height for
	// both, so the two plots read at the same size under the same legend and title rows.
	inline constexpr float kChartLines = 16.0f;

	// A bar tooltip's widest line, in font sizes, before a long label is cut or wrapped.
	inline constexpr float kTooltipEms = 36.0f;

	// Headroom added above the tallest bar and the line's peak when the value axes fit, as a
	// share of what they show. ImPlot's ApplyFit pads each end of the range it fits by half of
	// this; an axis held at zero gets only the half on top, about 6%.
	inline constexpr float kFitPadding = 0.12f;

	// A palette colour as ImPlot's style calls take it.
	ImVec4 PlotColour(ImU32 colour);

	// Whether the legend shows the series: an item ImPlot has not seen yet is shown, which is
	// how it starts. Only after SetupFinish, which pushes the id the items are hashed under.
	bool SeriesShown(const char* label);

	// Begins the chart's plot, the full width and kChartLines tall, with kFitPadding pushed
	// until EndBarChart: ImPlot reads it in EndPlot, where it fits the axes. `title` is centred
	// above the plot, on the line DrawAxisCaptions writes to; `id` goes behind "###", so a
	// change of title keeps the plot, and with it the reader's view and the legend's state.
	// Returns whether the plot is open; EndBarChart only when it is.
	bool BeginBarChart(const char* title, const char* id);
	void EndBarChart();

	// The value axes of a stacked-bar chart: the bars' on the left, and the line's on the
	// right when the chart has a line. Named above the plot by DrawAxisCaptions, level.
	struct BarChartValues
	{
		const char* BarCaption = nullptr;
		ImPlotScale BarScale = ImPlotScale_Linear;

		// Null for a chart without a line, which has no second axis.
		const char* LineCaption = nullptr;

		// Whether the line's axis stands on zero as the bars' does, rather than being fitted
		// to the line's own lowest and highest values.
		bool LineFromZero = false;
	};

	// After the x axis is set up: the value axes, both fitted to what is in view and the bars'
	// standing on zero; the legend outside and above, so it never covers the first bars;
	// SetupFinish, done by hand so the legend's show state can be read before anything is
	// plotted; and the axes' captions.
	void FinishBarChartSetup(const BarChartValues& values);

	// One layer of the bars, bottom up: its legend entry, its colour, and the top of the layer
	// for each bar, as StackedTops works it out - one per position PlotStackedBars is given.
	struct BarLayer
	{
		const char* Id = nullptr;
		ImVec4 Colour;
		std::span<const double> Tops;
	};

	// Plots `layers` on the first value axis, stacked by drawing each layer's bar from the top
	// down, every one over the last: PlotBarGroups takes its colours from the colormap and fixes
	// them when an item is first seen, where these must follow the scheme. A hidden layer is
	// still plotted, so its legend entry stays to show it again; ImPlot draws nothing for it,
	// and the layers above it come down by what it held.
	void PlotStackedBars(std::span<const BarLayer> layers, std::span<const double> xs, double width,
						 ImPlotBarsFlags flags = ImPlotBarsFlags_None);

	// Whether a column was clicked this frame, over the plot: taken on release and only without
	// a drag, which pans the chart. The drag distance outlives the release that ends it, which
	// is what makes it the test.
	bool ColumnClicked();

	// Whether the chart is being panned under the mouse now, when a bar's tooltip stays hidden.
	// Asked of the button held now: the drag distance ColumnClicked reads is kept until the
	// next click, and would hide the tooltip after any drag anywhere.
	bool ChartPanning();

	// One line of a bar's tooltip: a swatch in the layer's colour, its name, and its number,
	// and after the numbers' column an optional note in its own colour.
	struct SwatchRow
	{
		ImU32 Colour = 0;
		const char* Label = nullptr;
		std::string Number;
		std::string Note;
		ImU32 NoteColour = 0;
	};

	// The rows inside a tooltip, indented a font size, the labels in one column and the numbers
	// lined up on the right of the next, in `ink`.
	void DrawSwatchRows(std::span<const SwatchRow> rows, ImU32 ink);
}
