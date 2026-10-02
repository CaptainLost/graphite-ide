#pragma once

#include <cstddef>
#include <string_view>

namespace Core {

	struct OpenItem;

	struct IndentStyle
	{
		bool UseSpaces = true;
		std::size_t Width = 4;
	};

	bool InsertText(OpenItem& item, std::string_view utf8);

	bool DeleteBackward(OpenItem& item);

	bool DeleteForward(OpenItem& item);

	bool DeleteWordBackward(OpenItem& item);
	bool DeleteWordForward(OpenItem& item);

	bool Undo(OpenItem& item);
	bool Redo(OpenItem& item);

	void RefreshDerived(OpenItem& item);

	bool InsertNewline(OpenItem& item, IndentStyle style);

	bool Indent(OpenItem& item, IndentStyle style);

	bool Outdent(OpenItem& item, IndentStyle style);

	// Comments every non-blank line the selection touches — the caret's line when nothing is
	// selected — or, when every one of them already starts with `prefix` after its indentation,
	// uncomments them. The prefix is the caller's: Core knows no languages.
	bool ToggleLineComment(OpenItem& item, std::string_view prefix);
}