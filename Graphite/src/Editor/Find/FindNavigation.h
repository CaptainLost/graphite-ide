#pragma once

namespace Core {

	class TextBuffer;
	struct OpenItem;
}

namespace Graphite {

	// Rebuilds the document's find matches from its query and flags, unconditionally, and puts
	// CurrentMatch on the first match at or after the caret. Every place that changes the query or a
	// flag calls this as it changes them — see RefreshFindMatchesIfStale for why that matters.
	void RefreshFindMatches(Core::OpenItem& item, const Core::TextBuffer& buffer);

	// Rebuilds only when the buffer has moved on from the revision the list was built at, and
	// says whether it did. This is what the editor calls every frame, so the answer has to be
	// "no" on almost all of them: a rebuild scans the whole document, and doing that sixty times
	// a second is the difference between a find bar and a stall.
	//
	// The revision is the only thing compared, because it is the only change that does not come
	// through this file's own callers. The query and the flags have no revision of their own to
	// compare against — FindState keeps no copy of what the list was built from — so instead
	// every path that can change them rebuilds on the spot.
	bool RefreshFindMatchesIfStale(Core::OpenItem& item, const Core::TextBuffer& buffer);

	// What one frame of the find field reported: whether the widget called itself edited, and
	// whether the Escape that closes the bar was pressed on that same frame.
	struct FindFieldEvents
	{
		bool Edited = false;
		bool EscapePressed = false;
	};

	// Whether the field's text is the user's to keep. Escape makes an ImGui field revert to the
	// value it held when it was activated, and the field reports itself edited on that frame as
	// it would for any other change — so on the frame the bar closes, the "edit" is ImGui undoing
	// what was typed rather than the user typing it. Writing that back would overwrite the query
	// the bar exists to hand back the next time it opens.
	bool AcceptsQueryEdit(const FindFieldEvents& events);

	// Moves to the next match, or the previous one, wrapping at either end, and selects it:
	// anchor at the start, offset at the end. Does nothing when there are no matches. Asks for a
	// scroll rather than performing one — the caller is not necessarily drawing.
	void StepFindMatch(Core::OpenItem& item, const Core::TextBuffer& buffer, bool backwards);

	// Selects the current match and asks for a scroll to it, the way a step does, without
	// moving CurrentMatch. Does nothing when there are no matches.
	//
	// Called whenever the bar rebuilds the list on the user's behalf — a query typed, a toggle
	// flipped, the bar opened — because the counter names a current match and nothing else
	// showed which one it was: RefreshFindMatches only picks it, and it was often off screen.
	// Selecting it is also what keeps a query typed a letter at a time on the match it started
	// on, since the next rebuild picks from the selection's start.
	void RevealFindMatch(Core::OpenItem& item, const Core::TextBuffer& buffer);
}
