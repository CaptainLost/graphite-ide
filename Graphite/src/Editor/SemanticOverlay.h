#pragma once

#include "Lang/Api/LanguageService.h"
#include "Lang/Api/TokenKind.h"
#include "Lang/Syntax/Token.h"

#include <cstddef>
#include <map>
#include <vector>

namespace Core {

	class TextBuffer;
}

namespace Graphite {

	// One coloured range on one line, in the coordinates the lexer's own tokens use — offsets into
	// the tab-expanded text, because that is what SyntaxCache lexes and what the editor draws by
	// slicing. Converted once, when a reply arrives, so drawing a row converts nothing.
	struct SemanticSpan
	{
		std::size_t From = 0;
		std::size_t To = 0;
		TokenKind Kind = TokenKind::Text;
	};

	// What a language server said the identifiers in one document are, kept in the form the editor
	// draws from.
	//
	// A colour overlay and nothing more: it says what shade a span of an already-lexed line should
	// take, never what that line contains. A document with no server behind it has an empty
	// overlay and is drawn exactly as it was before any of this existed.
	//
	// Two coordinate systems meet here and both conversions are easy to get wrong in ways that
	// look like the colours sliding rather than like arithmetic. A server counts columns in UTF-16
	// code units of the raw line; the lexer was handed the line with its tabs expanded, and counts
	// bytes of that. So a position crosses UTF-16 units to raw bytes, and raw bytes to expanded
	// offsets — the first conversion needs the line's own text, the second needs the tab width,
	// and neither can stand in for the other.
	class SemanticOverlay
	{
	public:
		// Replaces everything. A reply is about the whole document, so anything an edit had shifted
		// in the meantime was a stopgap waiting for exactly this.
		void Set(const std::vector<SemanticToken>& tokens, const Core::TextBuffer& buffer,
				 std::size_t tabWidth);

		void Clear();

		// True for a document nothing has been said about yet, which is also true of one whose
		// every line a server declined to colour - the two are the same thing to a reader.
		bool IsEmpty() const { return m_Lines.empty(); }

		// Roughly what this overlay costs to keep, for the cache that budgets in bytes. Rough
		// deliberately: what a budget protects is an order of magnitude, and a faithful
		// accounting of a std::map's nodes would be unportable and beside the point.
		std::size_t Footprint() const;

		// There is deliberately no way to adjust an overlay for an edit. Colours stay where the
		// server put them until a reply replaces them: a colour that has slid a few characters is
		// visibly provisional and costs a reader nothing, while shifting one line-by-line only ever
		// approximated an answer that was already on its way.

		// Empty for a line nothing was said about, which is most of them: a server names
		// identifiers, and blank lines, comments and punctuation are nobody's.
		const std::vector<SemanticSpan>& SpansOn(std::size_t line) const;

	private:
		std::map<std::size_t, std::vector<SemanticSpan>> m_Lines;
	};

	// What colour a lexed token takes once the server has been heard from: its own kind unless
	// it is an identifier a span starts at, in which case the span's.
	//
	// Identifiers only. A server names them and nothing else, and a span that reached a comment
	// or a string would be recolouring something the lexer already knows better than it does.
	// Matched on the token's start rather than by overlap, because the two agree on where an
	// identifier begins — both measure the expanded line — and a start is one comparison where
	// an overlap is four.
	TokenKind ColourOf(const Token& token, const std::vector<SemanticSpan>& semantic);
}
