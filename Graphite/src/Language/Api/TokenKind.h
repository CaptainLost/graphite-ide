#pragma once

#include <cstdint>

namespace Graphite {

	// What the editor colours a span as. Deliberately smaller than any one language's grammar:
	// this is the vocabulary the palette and every later language share, so a kind earns its
	// place only when something is drawn differently for it.
	enum class TokenKind : std::uint8_t
	{
		Text,
		Keyword,
		Identifier,
		Number,
		String,
		Character,
		Comment,
		Preprocessor,
		Punctuation,
		Invalid,

		// What a language server says an identifier is, which a lexer cannot know: it sees
		// `Application`, `s_Instance` and `CORE_ASSERT` as one kind of word. These are drawn over
		// the lexer's own colouring where a server answers, and are simply never used where none
		// does - so a file with no server behind it looks exactly as it did before they existed.
		SemanticType,
		SemanticFunction,
		SemanticMacro,
		SemanticMember,
		SemanticParameter,
		SemanticNamespace,

		// Not a colourable kind — the count of the ones above it. Sizes SyntaxPalette::Colours
		// and must stay the last enumerator, so a new kind grows the palette instead of reading
		// past the end of it.
		Count
	};
}
