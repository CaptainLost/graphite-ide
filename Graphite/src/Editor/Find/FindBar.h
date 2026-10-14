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

		void Draw(Core::OpenItem& item, const Core::TextBuffer& buffer, bool documentFocused);

	private:
		// WARNING: Keyed on the path rather than the item's address, because a reallocation of the
		// open-document list hands one address to a different document.
		std::filesystem::path m_FocusFor;
	};
}
