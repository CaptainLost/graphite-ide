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
	struct SideBySidePanes
	{
		// Each half's share of the width. Both halves are clipped to this, so a line longer
		// than it is reached by scrolling rather than by spilling into the other half.
		float PaneWidth = 0.0f;

		// Where the right half's first column starts.
		float RightPaneX = 0.0f;
	};

	// `visibleWidth` is what the region can actually show — its clipping rectangle, which has
	// the border and any scrollbar already taken off. The panes divide what the reader sees.
	SideBySidePanes SplitIntoPanes(float visibleWidth);

	// What the region is told its content measures across, which decides whether a horizontal
	// scrollbar appears at all and how far it reaches.
	//
	// `innerWidth` is the region's inner width — the one ImGui compares against, which unlike
	// the panes' visible width still has the border in it. `widestLine` is the widest line
	// either half must draw, measured from where its text starts. `padding` is what the region
	// insets its content by on each side.
	//
	// The padding is why this is not simply a width plus a line. ImGui measures content from
	// the padded top-left corner but compares it against the unpadded inner width, adding the
	// padding back on both sides to work out the scroll range. A content width equal to the
	// inner width therefore leaves a scrollbar with two paddings of travel and nothing to
	// reveal — a scrollbar on every diff, however short its lines.
	//
	// A line that fits within its half yields no scroll range at all. A longer one yields
	// exactly enough for its last pixel to reach the inner edge of its own half, and no more,
	// so scrolling stops where the widest line ends rather than running on into blank space.
	// The line ends flush against the split at that point; leaving a gap there would mean
	// treating a line that visibly fits as one that needs scrolling, which is the worse of the
	// two.
	float SideBySideContentWidth(float innerWidth, float widestLine, float padding);
}
