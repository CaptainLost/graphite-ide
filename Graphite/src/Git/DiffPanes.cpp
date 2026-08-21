#include "Git/DiffPanes.h"

#include <algorithm>

namespace Graphite {

	SideBySideLayout LayoutSideBySide(float availableWidth, float widestLine, float padding)
	{
		SideBySideLayout layout;

		// A pane of negative width would put the right half left of the left one. The caller
		// gets this straight from ImGui, which reports a negative region while a pane is being
		// dragged smaller than its decorations.
		layout.PaneWidth = std::max(availableWidth, 0.0f) * 0.5f;
		layout.RightPaneX = layout.PaneWidth;

		// The line starts a padding in from its pane, so it needs that much more room than its
		// own width; the two paddings come off because the region adds them back itself when
		// it works out how far the scrollbar reaches. See the header for what goes wrong
		// without them.
		// Clamped because a padding wider than the region itself would otherwise ask for a
		// negative content width, which is not something to hand a scroll region.
		layout.ContentWidth =
			std::max(layout.PaneWidth - std::max(padding, 0.0f) * 2.0f +
						 std::max(layout.PaneWidth, widestLine + std::max(padding, 0.0f)),
					 0.0f);

		return layout;
	}
}
