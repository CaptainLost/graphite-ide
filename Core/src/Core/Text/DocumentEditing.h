#pragma once

#include <cstddef>
#include <string_view>

namespace Core {

	class TextDocument;
	struct Caret;

	struct IndentStyle
	{
		bool UseSpaces = true;
		std::size_t Width = 4;
	};

	// Each operation edits the document and moves only the caret it is given. Whoever holds other
	// carets or caches over the document catches up from its change log.
	bool InsertText(TextDocument& document, Caret& caret, std::string_view utf8);

	bool DeleteBackward(TextDocument& document, Caret& caret, std::size_t tabWidth);
	bool DeleteForward(TextDocument& document, Caret& caret, std::size_t tabWidth);

	bool DeleteWordBackward(TextDocument& document, Caret& caret, std::size_t tabWidth);
	bool DeleteWordForward(TextDocument& document, Caret& caret, std::size_t tabWidth);

	bool Undo(TextDocument& document, Caret& caret);
	bool Redo(TextDocument& document, Caret& caret);

	bool InsertNewline(TextDocument& document, Caret& caret);

	bool Indent(TextDocument& document, Caret& caret, IndentStyle style);

	bool Outdent(TextDocument& document, Caret& caret, IndentStyle style);

	bool ToggleLineComment(TextDocument& document, Caret& caret, std::string_view commentPrefix);

	// Moves the whole lines the selection touches one line up (a negative direction) or down, with the caret and the
	// selection on them; one undo step. False at the top or the bottom.
	bool MoveLines(TextDocument& document, Caret& caret, int direction);

	// Puts a copy of the whole lines the selection touches below them and takes the caret and the selection onto the
	// copy; one undo step.
	bool DuplicateLines(TextDocument& document, Caret& caret);
}
