#pragma once

#include <cstdint>

namespace Graphite {

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

		SemanticType,
		SemanticFunction,
		SemanticMacro,
		SemanticMember,
		SemanticParameter,
		SemanticNamespace,

		// WARNING: Count must stay the last enumerator; it sizes SyntaxPalette::Colours.
		Count
	};

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
