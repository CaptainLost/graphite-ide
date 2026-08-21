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

		// What the region is told its content measures across, which is what decides whether a
		// horizontal scrollbar appears at all and how far it reaches.
		//
		// The padding is why this is not simply a width plus a line. ImGui measures content
		// from the padded top-left corner but compares it against the unpadded inner width,
		// adding the padding back on both sides to decide the scroll range. A content width
		// equal to the visible width therefore leaves a scrollbar with two paddings of travel
		// and no content to reveal — a scrollbar on every diff, however short its lines. Taking
		// the padding off here is what makes a diff that fits have nothing to scroll.
		float ContentWidth = 0.0f;
	};

	// `widestLine` is the widest line either half must draw, measured from where its text
	// starts. `padding` is what the region insets its content by on each side.
	//
	// A line that fits within its pane yields a content width equal to the visible width less
	// its two paddings, so no horizontal scrollbar appears. A longer one yields exactly enough
	// for its last pixel to reach the inner edge of its own pane, and no more: scrolling stops
	// where the widest line ends rather than running on into blank space.
	SideBySideLayout LayoutSideBySide(float availableWidth, float widestLine, float padding);
}
