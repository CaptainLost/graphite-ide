#pragma once

#include <filesystem>

namespace Core {

	struct OpenItem;
	class TextBuffer;
}

namespace Graphite {

	// The find overlay: the query field, its counter, the toggles and the buttons that step
	// through what it found. Split out of TextEditorWidget, which was carrying it alongside the
	// row drawing, the gutter, the caret and the input handling despite the two sharing nothing.
	//
	// A class rather than free functions because there is one thing to own. Everything else the
	// bar knows lives on the document, in Core::OpenItem::Find, and survives a tab switch with
	// it — but which document has asked for the keyboard on its next frame belongs to no
	// document, and was a std::filesystem::path at file scope until this existed. ADR 0033
	// forbids exactly that: a global with a non-trivial constructor.
	class FindBar
	{
	public:
		// Opens the bar for a document, seeds the query from its selection when that selection is
		// worth seeding from, and asks for the keyboard on the next frame that document draws.
		void Open(Core::OpenItem& item);

		// Draws the bar over the editor surface currently being drawn into, and applies the keys
		// that belong to it. Call only while that surface is the current ImGui window: the bar
		// works out its own corner from it.
		//
		// `documentFocused` is the caller's answer to whether this document holds the keyboard,
		// root and children — the bar's own field counts, since Escape and F3 are as much the
		// field's keys as the document's.
		void Draw(Core::OpenItem& item, const Core::TextBuffer& buffer, bool documentFocused);

	private:
		// Keyed on the path rather than the item's address: a reallocation of the open-document
		// list hands one address to a different document.
		std::filesystem::path m_FocusFor;
	};
}
