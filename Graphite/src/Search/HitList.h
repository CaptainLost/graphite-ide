#pragma once

#include "Search/HitGroups.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	// Which hit a results list is previewing. Keyed on the group's id — the file's full path —
	// and the hit's index within that group, not on a pointer: hits are appended to groups as
	// results arrive and the vector holding them reallocates.
	struct HitSelection
	{
		std::string Group;
		std::size_t Hit = 0;
	};

	// Bundled rather than passed one at a time, matching SearchPreviewInput next door: three of
	// these are related by name alone, and a positional list would let a caller transpose them
	// with nothing said.
	struct HitListInput
	{
		// Not const, because of OnGroupOpened below: a caller that fills its rows lazily writes
		// into the group it is handed.
		std::vector<HitGroup>& Groups;

		// Written when a row is picked, by click or by keyboard focus landing on it, so a caller
		// can drive its own preview from it. Read back to know which row to draw selected.
		std::optional<HitSelection>& Selected;

		// Called in addition to selecting, on the second click of a double click — the search
		// panel's own rule, applied here so a references panel opens the same way: one click
		// previews, two open.
		const std::function<void(const SearchHit&)>& OnOpen;

		// Called with a group whose rows are about to be drawn, before any of them is, so a caller
		// whose hits do not yet carry the text they show can read it now — one file, when the user
		// expands it, rather than every file named by a reply in the frame that reply arrives.
		// Null for a caller whose hits arrive complete, which is what a file-tree search's do.
		const std::function<void(HitGroup&)>* OnGroupOpened = nullptr;
	};

	// Draws the tree of groups and their hits both the search panel and the references panel
	// show: grouped by file, clipped so an expanded file with thousands of hits costs only what
	// scrolls into view, and coloured with the match highlighted through HitDisplay.
	//
	// Call inside the child the results occupy; this does not open one.
	void DrawHitList(const HitListInput& input);
}
