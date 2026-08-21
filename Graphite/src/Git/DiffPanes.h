#pragma once

namespace Graphite {

	// Where the two halves of a side-by-side diff sit inside the single scrolling region that
	// holds them both, in the coordinates ImGui's cursor uses.
	//
	// One region rather than one per half is what keeps the halves together: two sibling child
	// windows each own their scroll position, so scrolling one leaves the other behind, and no
	// amount of copying one position to the other fixes that without a frame of lag. Sharing
	// the region means there is only ever one position to be in.
	//
	// The cost is that the horizontal scrollbar moves both halves at once, so a line long on
	// one side drags the other side along with it. For a diff that is the right trade: both
	// sides are the same file, and the reader wants the same column of each.
	struct SideBySideLayout
	{
		// Each half's share of the visible width. Both halves are clipped to this, so a line
		// longer than it is reached by scrolling rather than by spilling into the other half.
		float PaneWidth = 0.0f;

		// Where the right half's first column starts.
		float RightPaneX = 0.0f;

		// What the region is told its content measures across. Sized so the scrollbar reveals
		// exactly the widest line and stops: the right half already starts a pane in, so the
		// content runs one pane plus the widest line either half needs.
		float ContentWidth = 0.0f;
	};

	// `widestLine` is the widest line either half needs to draw, measured from the pane's own
	// left edge. Passing a width no wider than a pane yields a content width equal to the
	// visible width, so no horizontal scrollbar appears at all.
	SideBySideLayout LayoutSideBySide(float availableWidth, float widestLine);
}
