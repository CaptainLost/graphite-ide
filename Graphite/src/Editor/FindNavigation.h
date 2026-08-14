#pragma once

namespace Core {

	class TextBuffer;
	struct OpenItem;
}

namespace Graphite {

	// Rebuilds the document's find matches from its query and flags, unconditionally, and puts
	// Current on the first match at or after the caret. Every place that changes the query or a
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

	// Moves to the next match, or the previous one, wrapping at either end, and selects it:
	// anchor at the start, offset at the end. Does nothing when there are no matches. Asks for a
	// scroll rather than performing one — the caller is not necessarily drawing.
	void StepFindMatch(Core::OpenItem& item, const Core::TextBuffer& buffer, bool backwards);
}
