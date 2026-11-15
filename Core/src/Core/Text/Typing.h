#pragma once

#include "Core/Text/DocumentEditing.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace Core {

	class TextBuffer;
	class TextDocument;
	struct Caret;

	// The closing partner of an opening bracket, or of a quote, which closes itself.
	std::optional<char> ClosingPartnerOf(char opening);

	// One more level of indentation in the style the given indentation already uses: a tab-indented line
	// gains a tab and a space-indented one gains spaces whatever the setting says, so a file keeps its own style.
	std::string IndentUnitFor(std::string_view existingIndent, IndentStyle style);

	// The indentation a new line below the given one takes: that line's own, one level deeper when it ends in an
	// opening bracket.
	std::string IndentAfterLine(std::string_view lineText, IndentStyle style);

	enum class BracketPairing
	{
		// Brackets and quotes come in pairs: the partner is added, stepped over, put around a selection.
		Close,

		// Every character goes in as typed; a closing bracket on a line of only indentation still finds its place.
		Leave
	};

	// One typed character, with the help a code editor gives around brackets and quotes:
	// - over a selection, an opening bracket or a quote puts the pair around it and keeps the text selected;
	// - an opening bracket or a quote typed before whitespace, the line's end or a closing character brings its
	//   partner, with the caret between them; a quote does not after a letter or inside a string of its own kind;
	// - a closing bracket typed in front of the same bracket, or a quote in front of the quote that ends its
	//   string, steps over it;
	// - a closing bracket typed on a line holding only indentation takes the indentation of the line it closes.
	// Anything else is inserted as typed. With BracketPairing::Leave only the last of these applies.
	bool TypeCharacter(TextDocument& document, Caret& caret, std::string_view utf8, BracketPairing pairing);

	// Backspace: between an opening bracket or quote and its partner, both go.
	bool DeleteBackwardAcrossPair(TextDocument& document, Caret& caret, std::size_t tabWidth);

	// Pastes text of several lines at the indentation where it lands: the lines after the first keep their
	// indentation relative to one another and move as a block to the landing line's. Pasted at a line's
	// indentation, the first line moves with them; after text, it continues the line as copied. One undo step.
	bool PasteText(TextDocument& document, Caret& caret, std::string_view utf8, IndentStyle style);

	// The arithmetic of PasteText: the pasted lines, every line ending already a newline, with the shallowest of
	// the lines that move brought to baseIndent and the others kept that much deeper. The first line moves only
	// when firstLineMoves; blank lines come out empty.
	std::string ReindentPasted(std::string_view pasted, std::string_view baseIndent, bool firstLineMoves,
							   IndentStyle style);
}
