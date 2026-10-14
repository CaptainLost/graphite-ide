#pragma once

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
}
