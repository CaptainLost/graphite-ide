#pragma once

#include <imgui.h>

namespace Graphite {

	class FollowingAxis
	{
	public:
		void Refit() { m_FitPending = true; }

		void Reset()
		{
			m_FitPending = true;
			m_ViewMoved = false;
		}

		// WARNING: call between SetupAxis(ImAxis_X1, ...) and SetupFinish.
		void Setup(double low, double high);

		// WARNING: call after SetupFinish and before EndPlot.
		void Track();

	private:
		bool m_FitPending = true;
		bool m_ViewMoved = false;
	};

	// WARNING: call after SetupFinish, which places the plot. The plot must have a title: ImPlot
	// centres it on a line of its own between the legend and the plot, which is where these go.
	void DrawAxisCaptions(const char* left, const char* right);

	// WARNING: call between SetupAxis(ImAxis_X1, ...) and SetupFinish.
	void SetupCountedAxis();

	// WARNING: call after SetupFinish and before the bars are plotted, so the band lies under them.
	void DrawColumnBand(double x, double slot, ImU32 accent);
}
