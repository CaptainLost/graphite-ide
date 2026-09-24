#pragma once

namespace Graphite {

	// How wide each of two panes drawn side by side is, the one on the left sized by the reader
	// dragging the handle between them.
	struct SideBySideWidths
	{
		float Left = 0.0f;
		float Right = 0.0f;
	};

	// Splits `room` — the width the two panes have between them, once the handle and the
	// spacing around it are taken off — giving the left pane `wanted` as far as that leaves
	// each side `minimum`. The right pane takes the rest.
	//
	// When the room is short of two minimums, each side's minimum becomes half the room, so
	// both get half rather than the left keeping its minimum and pushing the right past the
	// edge: the two together are never wider than the room, which is what keeps the pane they
	// are drawn in from growing a horizontal scrollbar. Neither is ever negative, and a room
	// of zero or less leaves both at zero.
	//
	// The left width is not rounded, so a drag that moves it by a fraction of a pixel a frame
	// still adds up. A child window drawn at it is cut to whole pixels, which only leaves a
	// right pane that fills the rest a fraction wider than Right, and the two still within the
	// room.
	SideBySideWidths SplitSideBySide(float room, float wanted, float minimum);
}
