#include "Git/Diff/DiffPanes.h"

#include <algorithm>

namespace Graphite {

	namespace {

		constexpr float HalfShare = 0.5f;
		constexpr float InsetsPerRegion = 2.0f;
		constexpr float NoWidth = 0.0f;
	}

	SideBySidePanes SplitIntoPanes(float visibleWidth)
	{
		SideBySidePanes panes;

		// WARNING: ImGui reports a negative region while a pane is dragged smaller than its
		// decorations; a negative pane width would put the right half left of the left one.
		const float nonNegativeWidth = std::max(visibleWidth, NoWidth);
		panes.PaneWidth = nonNegativeWidth * HalfShare;
		panes.RightPaneX = panes.PaneWidth;

		return panes;
	}

	float SideBySideContentWidth(float innerWidth, float widestLine, float padding)
	{
		const float halfWidth = std::max(innerWidth, NoWidth) * HalfShare;
		const float inset = std::max(padding, NoWidth);

		const float widestLineWithInset = widestLine + inset;
		const float roomForWidestLine = std::max(halfWidth, widestLineWithInset);
		const float scrollRangeContribution = halfWidth - inset * InsetsPerRegion;

		// WARNING: Clamped because an inset wider than the region would ask for a negative
		// content width, which a scroll region cannot take.
		return std::max(scrollRangeContribution + roomForWidestLine, NoWidth);
	}
}
