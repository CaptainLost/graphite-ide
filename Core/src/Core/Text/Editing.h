#pragma once

#include "Core/Text/DocumentEditing.h"

#include <cstddef>
#include <string_view>

namespace Core {

	struct OpenItem;

	bool InsertText(OpenItem& item, std::string_view utf8);

	bool DeleteBackward(OpenItem& item);

	bool DeleteForward(OpenItem& item);

	bool DeleteWordBackward(OpenItem& item);
	bool DeleteWordForward(OpenItem& item);

	bool Undo(OpenItem& item);
	bool Redo(OpenItem& item);

	void RecomputeLineCaches(OpenItem& item);

	enum class CaretFollow
	{
		// The edit that moved the text also placed the caret.
		AlreadyPlaced,

		// Someone else edited the text; the caret moves with it.
		Shift
	};

	// Brings the item's line caches, and with Shift its caret, up to its document's text.
	void CatchUpWithDocument(OpenItem& item, CaretFollow follow);

	bool InsertNewline(OpenItem& item);

	bool Indent(OpenItem& item, IndentStyle style);

	bool Outdent(OpenItem& item, IndentStyle style);

	bool ToggleLineComment(OpenItem& item, std::string_view commentPrefix);
}