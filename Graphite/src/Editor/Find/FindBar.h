#pragma once

#include <filesystem>

namespace Core {

	struct OpenItem;
	class TextBuffer;
}

namespace Graphite {

	class FindBar
	{
	public:
		void Open(Core::OpenItem& item);

		// escapeTaken: something in front of the bar, the navigation bubble or the line peek, took this frame's Escape,
		// which then leaves the bar open.
		void Draw(Core::OpenItem& item, const Core::TextBuffer& buffer, bool documentFocused, bool escapeTaken);

	private:
		// WARNING: Keyed on the path rather than the item's address, because a reallocation of the
		// open-document list hands one address to a different document.
		std::filesystem::path m_FocusFor;
	};
}
