#pragma once

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

	// One row of a list, clickable across its whole width, with the matched part of its label
	// marked. Returns what Selectable returns.
	//
	// `id` must start with "##": the label is drawn separately, in pieces, over an empty
	// Selectable — positioning a coloured rectangle behind one string instead would mean
	// measuring text, and CalcTextSize rounds up.
	bool HighlightedSelectable(const char* id, bool selected, std::string_view label,
							   std::string_view needle);
}
