#pragma once

#include <cstddef>
#include <optional>

namespace Core {

	class TextBuffer;
}

namespace Graphite {

	class SyntaxCache;

	// How far the partner search reads in each direction before giving up. Well past a
	// screenful and well short of a document, which is the only property the number needs: an
	// unmatched brace at the top of a 40 000-line file must not walk the whole of it every
	// time the caret moves.
	inline constexpr std::size_t kBracketScanLines = 500;

	// The two brackets to highlight. When Matched is false the caret is on a bracket with no
	// partner and both offsets are that bracket's own.
	struct BracketPair
	{
		std::size_t Open = 0;
		std::size_t Close = 0;
		bool Matched = false;
	};

	// The pair for the bracket the caret is on, or nothing when it is on none. The bracket
	// *after* the caret wins; failing that, the one before it — which is what editors people
	// arrive from do, and being surprising here is worse than being simple.
	//
	// Brackets inside strings, characters, comments and preprocessor lines are text: the caret
	// finds nothing on one, and the partner search reads past them. The syntax cache is what
	// says which is which, so there is no second scanner here to disagree with the colouring on
	// screen. The cache is taken by non-const reference because asking it for a line's tokens
	// is what extends it.
	//
	// The tab width comes from the cache rather than from a parameter: token offsets are
	// offsets into the tab-expanded line, so the only width that can map a raw byte onto them
	// is the one the tokens were produced with. A width passed in beside the cache could
	// disagree with it, and the mapping would land silently in the wrong token.
	std::optional<BracketPair> FindBracketPair(const Core::TextBuffer& buffer,
											   SyntaxCache& syntax, std::size_t caret);
}
