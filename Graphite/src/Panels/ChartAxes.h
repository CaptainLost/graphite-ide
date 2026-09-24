#pragma once

namespace Graphite {

	// A plot's x axis that follows its data — fitted to every point whenever they change — until
	// the reader pans or zooms it, and follows again after a double click, which ImPlot answers
	// by fitting everything. Shared by the session tabs' charts so each pans the same way.
	class FollowingAxis
	{
	public:
		// The points changed: fit the axis to them on the next frame, unless the reader has
		// moved it.
		void Refit() { m_FitPending = true; }

		// The points are another set altogether — another reading of the transcript, another
		// unit along the axis: where the reader had moved to means nothing now.
		void Reset()
		{
			m_FitPending = true;
			m_ViewMoved = false;
		}

		// Between SetupAxis(ImAxis_X1, ...) and SetupFinish: fits the axis to [low, high] when a
		// fit is due and the reader has not moved it. Set with ImPlotCond_Always for that one
		// frame only, since an axis set that way takes no input.
		void Setup(double low, double high);

		// After SetupFinish, before EndPlot: notices the reader panning or zooming the axis —
		// over the plot or over its tick area, which ImPlot pans from too — or double clicking
		// to fit it again. A click on the legend is neither.
		void Track();

	private:
		bool m_FitPending = true;
		bool m_ViewMoved = false;
	};

	// The value axes' names, written level above each end of the plot instead of turned on
	// their side beside it; `right` is null for a plot with no second axis. After SetupFinish,
	// which has placed the plot. The plot is begun with a title, which ImPlot centres on a line
	// of its own between the legend and the plot; these go at the two ends of that line.
	// Without one they would share the legend's band, and a wide legend would cover them.
	void DrawAxisCaptions(const char* left, const char* right);

	// Labels only the whole numbers on the x axis: turns and requests are counted, and a tick
	// at 1.5 names neither. Between SetupAxis(ImAxis_X1, ...) and SetupFinish.
	void SetupCountedAxis();
}
