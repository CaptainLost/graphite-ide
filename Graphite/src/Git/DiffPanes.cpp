#include "Git/DiffPanes.h"

#include <algorithm>

namespace Graphite {

	SideBySideLayout LayoutSideBySide(float availableWidth, float widestLine)
	{
		SideBySideLayout layout;

		// A pane of negative width would put the right half left of the left one. The caller
		// gets this straight from ImGui, which reports a negative region while a pane is being
		// dragged smaller than its decorations.
		layout.PaneWidth = std::max(availableWidth, 0.0f) * 0.5f;
		layout.RightPaneX = layout.PaneWidth;
		layout.ContentWidth = layout.PaneWidth + std::max(layout.PaneWidth, widestLine);

		return layout;
	}
}
