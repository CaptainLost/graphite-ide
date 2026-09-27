#pragma once

#include <imgui.h>

#include <string>
#include <string_view>

namespace Graphite {

	// A row's label cut into the three pieces it is drawn in: the text before the match, the
	// match itself, and the text after it. Views into the label, so it must outlive them —
	// the same contract, and the same reason, as the search panel's HitLine.
	struct HighlightedLabel
	{
		std::string_view Before;
		std::string_view Match;
		std::string_view After;
	};

	// The first place `needle` occurs in `label`, ignoring case and the whitespace around the
	// needle. A needle that is empty, whitespace alone, or absent leaves the whole label in
	// Before and the other two empty — a row with nothing to mark is a row drawn plainly.
	//
	// The match keeps the label's own case: the row shows what the commit says, and the needle
	// only says where to mark it.
	//
	// Only the first occurrence, because one mark per row is what the eye is for. Marking every
	// occurrence turns a row of common words into a mostly-highlighted row.
	HighlightedLabel SplitHighlight(std::string_view label, std::string_view needle);

	// Deleted for the reason the search panel's own splitter is: a temporary label returns
	// views into a string destroyed before they are drawn, which is garbage on screen rather
	// than a crash.
	HighlightedLabel SplitHighlight(std::string&&, std::string_view) = delete;

	// One row of a list, clickable across its whole width: a leading marker that may carry a
	// colour of its own — a file row's change letter — followed by text with the reader's
	// needle marked wherever it appears. Returns what Selectable returns.
	//
	// `text` is the rest of the label exactly as it should appear, spacing included: how far
	// the text sits from the marker is the caller's layout and not this function's.
	//
	// With nothing to colour and nothing to mark, this is one plain Selectable carrying its
	// own label, which is what every other row in the application is. Otherwise the label is
	// drawn in pieces over an empty Selectable — positioning a coloured rectangle behind one
	// string instead would mean measuring text, and CalcTextSize rounds up.
	//
	// `id` must start with "##", because it is that empty Selectable's id.
	bool MarkedSelectable(const char* id, bool selected, std::string_view marker,
						  ImU32 markerColor, std::string_view text,
						  std::string_view needle = {});

	// A row with no marker: all label, needle marked. The refs list.
	bool HighlightedSelectable(const char* id, bool selected, std::string_view label,
							   std::string_view needle);
}
