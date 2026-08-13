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
		Invalid
	};
}
