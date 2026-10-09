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

		// A line lexer cannot tell `Application`, `s_Instance` and `CORE_ASSERT` apart, so a
		// grammar may only guess these; a server's answer overrules the guess.
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

	// A grammar guesses at what a name is and a server may overrule it, so a name can arrive as any of these.
	inline bool NamesSomething(TokenKind kind)
	{
		switch (kind)
		{
		case TokenKind::Identifier:
		case TokenKind::SemanticType:
		case TokenKind::SemanticFunction:
		case TokenKind::SemanticMacro:
		case TokenKind::SemanticMember:
		case TokenKind::SemanticParameter:
		case TokenKind::SemanticNamespace:
			return true;
		case TokenKind::Text:
		case TokenKind::Keyword:
		case TokenKind::Number:
		case TokenKind::String:
		case TokenKind::Character:
		case TokenKind::Comment:
		case TokenKind::Preprocessor:
		case TokenKind::Punctuation:
		case TokenKind::Invalid:
		case TokenKind::Count:
			return false;
		}

		return false;
	}
}
