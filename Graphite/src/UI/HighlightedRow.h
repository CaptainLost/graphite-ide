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

	// A row whose middle piece carries a colour of its own: `lead` drawn plainly, then
	// `tinted` in `tint`, then `rest` with the needle marked. The commit rows, whose hash is
	// tinted while the commit is not on the upstream yet.
	//
	// Unlike MarkedSelectable's marker, the coloured piece is not the first thing in the
	// row, which is the whole reason this exists: a commit row opens with its expander, and
	// the piece worth colouring comes after it.
	//
	// One mark is drawn, not one per piece — the same rule SplitHighlight states for one
	// label. `tinted` is searched first because it is the earlier piece, and a needle found
	// there is marked instead of tinted: the reader asked where their text landed, and an
	// answer they can see beats a colour they were not looking for. A needle straddling the
	// boundary between the two pieces matches neither, which is the one thing this cannot do
	// that a single label could.
	//
	// A zero `tint` draws the middle piece plainly, so a caller with nothing to colour does
	// not need a different function.
	//
	// `id` must start with "##", as MarkedSelectable's must, and for the same reason.
	//
	// `hovered`, when asked for, answers whether the pointer is over the row — the row's own
	// item, and not the last piece drawn over it. A caller cannot work this out afterwards
	// with ImGui::IsItemHovered(): by the time this returns, the last item is a run of text
	// laid on top, and asking it gives the wrong answer for exactly the rows this function
	// exists to draw.
	bool TintedSelectable(const char* id, bool selected, std::string_view lead,
						  std::string_view tinted, ImU32 tint, std::string_view rest,
						  std::string_view needle = {}, bool* hovered = nullptr);

	// A row with no marker: all label, needle marked. The commit rows and the refs list.
	bool HighlightedSelectable(const char* id, bool selected, std::string_view label,
							   std::string_view needle);
}
