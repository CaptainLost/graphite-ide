#include "Git/DiffPanes.h"

#include <algorithm>

namespace Graphite {

	SideBySidePanes SplitIntoPanes(float visibleWidth)
	{
		SideBySidePanes panes;

		// A pane of negative width would put the right half left of the left one. The caller
		// gets this straight from ImGui, which reports a negative region while a pane is being
		// dragged smaller than its decorations.
		panes.PaneWidth = std::max(visibleWidth, 0.0f) * 0.5f;
		panes.RightPaneX = panes.PaneWidth;

		return panes;
	}

	float SideBySideContentWidth(float innerWidth, float widestLine, float padding)
	{
		const float half = std::max(innerWidth, 0.0f) * 0.5f;
		const float inset = std::max(padding, 0.0f);

		// The line starts an inset in from its half, so it needs that much more room than its
		// own width; the two insets come off because the region adds them back itself when it
		// works out how far the scrollbar reaches. See the header for what goes wrong without
		// them. Clamped because an inset wider than the region would otherwise ask for a
		// negative content width, which is not something to hand a scroll region.
		return std::max(half - inset * 2.0f + std::max(half, widestLine + inset), 0.0f);
	}
}
