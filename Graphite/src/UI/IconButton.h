#pragma once

namespace Graphite {

	// The side of an IconButton's square. Smaller than a frame, so a row of them sits inside
	// the line with air around each rather than filling it edge to edge. Exposed for callers
	// that reserve room for a fixed number of buttons before drawing them.
	float IconButtonSize();

	// A square button carrying one icon glyph, centred against the frame height of the line it
	// is drawn on — so a line whose first item is a frame (a text field, or text aligned with
	// AlignTextToFramePadding) keeps its buttons on the same centre line.
	//
	// `engaged` is for toggles: an engaged button keeps a fill at rest and draws its glyph in
	// the accent colour, so "on" is visible without hovering it. `tooltip` is shown on hover
	// and is not optional — an icon says nothing to someone meeting it for the first time.
	bool DrawIconButton(const char* id, const char* glyph, bool engaged, const char* tooltip);

	// How much of the line DrawIconSeparator claims, for callers that reserve room for it.
	constexpr float kIconSeparatorWidth = 1.0f;

	// A hairline between groups of icon buttons, centred against the frame height the same
	// way they are.
	void DrawIconSeparator();
}
