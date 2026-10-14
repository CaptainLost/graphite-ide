#pragma once

namespace Graphite {

	// WARNING: Both halves live in one scrolling region, because two sibling child windows each
	// own a scroll position and one would lag a frame behind the other. The horizontal scrollbar
	// therefore moves both halves at once.
	struct SideBySidePanes
	{
		float PaneWidth = 0.0f;
		float RightPaneX = 0.0f;
	};

	// visibleWidth is the region's clipping rectangle, with border and scrollbar already taken off.
	SideBySidePanes SplitIntoPanes(float visibleWidth);

	// innerWidth is the region's inner width, which still has the border in it. widestLine is
	// measured from where its half's text starts. padding is the region's inset on each side.
	//
	// WARNING: ImGui measures content from the padded corner but compares it against the unpadded
	// inner width, adding the padding back on both sides to find the scroll range. A content width
	// equal to the inner width therefore shows a scrollbar on every diff.
	float SideBySideContentWidth(float innerWidth, float widestLine, float padding);
}
