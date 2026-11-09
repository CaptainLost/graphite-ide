#pragma once

#include <cstddef>

namespace Core {

	class TextBuffer;
	struct OpenItem;
}

namespace Graphite {

	enum class StepDirection
	{
		Forwards,
		Backwards,
	};

	struct FindFieldEvents
	{
		bool Edited = false;
		bool EscapePressed = false;
	};

	void RefreshFindMatches(Core::OpenItem& item, const Core::TextBuffer& buffer);

	bool RefreshFindMatchesIfStale(Core::OpenItem& item, const Core::TextBuffer& buffer);

	bool AcceptsQueryEdit(const FindFieldEvents& events);

	void StepFindMatch(Core::OpenItem& item, const Core::TextBuffer& buffer,
					   StepDirection direction);

	void RevealFindMatch(Core::OpenItem& item, const Core::TextBuffer& buffer);

	// Replaces the current match with the find state's replacement, as one undo step, and selects the next match.
	// False when there is nothing to replace or the document is read-only.
	bool ReplaceCurrentMatch(Core::OpenItem& item);

	// Replaces every match at once, as one undo step; how many were replaced.
	std::size_t ReplaceAllMatches(Core::OpenItem& item);
}
