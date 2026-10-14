#include "Editor/Hover/HoverColouring.h"

#include <array>
#include <cstddef>
#include <utility>

namespace Graphite {

	namespace {

		using Tokens = std::vector<Token>;

		constexpr std::size_t kFirstToken = 0;
		constexpr std::size_t kTokensBeforeAkaType = 2;
		constexpr std::size_t kTokensInArrow = 2;
		constexpr std::size_t kHoverKindCount = 21;

		std::string_view TokenText(std::string_view text, const Token& token)
		{
			return text.substr(token.From, token.To - token.From);
		}

		bool IsPunctuation(std::string_view text, const Tokens& tokens, std::size_t index,
						   char character)
		{
			return index < tokens.size() && tokens[index].Kind == TokenKind::Punctuation &&
				   text[tokens[index].From] == character;
		}

		bool Qualifies(std::string_view text, const Tokens& tokens, std::size_t index)
		{
			return IsPunctuation(text, tokens, index + 1, ':') &&
				   IsPunctuation(text, tokens, index + 2, ':');
		}

		bool IsDeclaredName(std::string_view text, const Tokens& tokens, std::size_t index,
							std::string_view name)
		{
			return tokens[index].Kind == TokenKind::Identifier &&
				   TokenText(text, tokens[index]) == name && !Qualifies(text, tokens, index);
		}

		void AddSpan(std::vector<SemanticSpan>& spans, const Token& token, TokenKind kind)
		{
			spans.push_back({token.From, token.To, kind});
		}

		// WARNING: Angle brackets count as nesting because a parameter's type can be a template; a
		// comparison could only appear in a default argument, which is never walked this way.
		int Nesting(std::string_view text, const Tokens& tokens, std::size_t index)
		{
			if (tokens[index].Kind != TokenKind::Punctuation)
			{
				return 0;
			}

			switch (text[tokens[index].From])
			{
			case '(':
			case '[':
			case '{':
			case '<':
				return 1;
			case ')':
			case ']':
			case '}':
			case '>':
				return -1;
			default:
				return 0;
			}
		}

		void TypesIn(std::string_view text, const Tokens& tokens, std::size_t firstToken,
					 std::size_t endToken, std::size_t skippedToken,
					 std::vector<SemanticSpan>& spans)
		{
			int squareBracketDepth = 0;

			for (std::size_t index = firstToken; index < endToken; ++index)
			{
				if (IsPunctuation(text, tokens, index, '['))
				{
					++squareBracketDepth;
				}
				else if (IsPunctuation(text, tokens, index, ']'))
				{
					--squareBracketDepth;
				}
				else if (index != skippedToken && squareBracketDepth == 0 &&
						 tokens[index].Kind == TokenKind::Identifier)
				{
					AddSpan(spans, tokens[index],
							Qualifies(text, tokens, index) ? TokenKind::SemanticNamespace
														   : TokenKind::SemanticType);
				}
			}
		}

		std::size_t AkaIn(std::string_view text, const Tokens& tokens, std::size_t firstToken,
						  std::size_t endToken)
		{
			int depth = 0;

			for (std::size_t index = firstToken; index + 1 < endToken; ++index)
			{
				if (depth == 0 && IsPunctuation(text, tokens, index, '(') &&
					tokens[index + 1].Kind == TokenKind::Identifier &&
					TokenText(text, tokens[index + 1]) == "aka")
				{
					return index;
				}

				depth += Nesting(text, tokens, index);
			}

			return endToken;
		}

		void AkaTypes(std::string_view text, const Tokens& tokens, std::size_t akaIndex,
					  std::size_t endToken, std::vector<SemanticSpan>& spans)
		{
			if (akaIndex + kTokensBeforeAkaType <= endToken)
			{
				TypesIn(text, tokens, akaIndex + kTokensBeforeAkaType, endToken, endToken, spans);
			}
		}

		void ParameterIn(std::string_view text, const Tokens& tokens, std::size_t firstToken,
						 std::size_t endToken, std::vector<SemanticSpan>& spans)
		{
			const std::size_t akaIndex = AkaIn(text, tokens, firstToken, endToken);
			AkaTypes(text, tokens, akaIndex, endToken, spans);
			endToken = akaIndex;

			std::size_t declaratorEnd = endToken;
			int depth = 0;

			for (std::size_t index = firstToken; index < endToken; ++index)
			{
				depth += Nesting(text, tokens, index);

				if (depth == 0 && IsPunctuation(text, tokens, index, '='))
				{
					declaratorEnd = index;
					break;
				}
			}

			std::size_t nameIndex = declaratorEnd;
			depth = 0;

			for (std::size_t index = firstToken; index < declaratorEnd; ++index)
			{
				depth += Nesting(text, tokens, index);

				if (depth == 0 && tokens[index].Kind == TokenKind::Identifier)
				{
					nameIndex = index;
				}
			}

			if (nameIndex != declaratorEnd && nameIndex > firstToken)
			{
				const Token& before = tokens[nameIndex - 1];

				const bool afterType = before.Kind == TokenKind::Identifier ||
									   before.Kind == TokenKind::Keyword ||
									   IsPunctuation(text, tokens, nameIndex - 1, '*') ||
									   IsPunctuation(text, tokens, nameIndex - 1, '&') ||
									   IsPunctuation(text, tokens, nameIndex - 1, '>');

				if (!afterType)
				{
					nameIndex = declaratorEnd;
				}
			}
			else
			{
				nameIndex = declaratorEnd;
			}

			if (nameIndex != declaratorEnd)
			{
				AddSpan(spans, tokens[nameIndex], TokenKind::SemanticParameter);
			}

			TypesIn(text, tokens, firstToken, declaratorEnd, nameIndex, spans);
		}
	}

	TokenKind KindOfHoverSymbol(std::string_view hoverKind)
	{
		constexpr std::array<std::pair<std::string_view, TokenKind>, kHoverKindCount> kKinds{{
			{"function", TokenKind::SemanticFunction},
			{"instance method", TokenKind::SemanticFunction},
			{"class method", TokenKind::SemanticFunction},
			{"static method", TokenKind::SemanticFunction},
			{"constructor", TokenKind::SemanticFunction},
			{"destructor", TokenKind::SemanticFunction},
			{"conversion function", TokenKind::SemanticFunction},
			{"class", TokenKind::SemanticType},
			{"struct", TokenKind::SemanticType},
			{"union", TokenKind::SemanticType},
			{"enum", TokenKind::SemanticType},
			{"type alias", TokenKind::SemanticType},
			{"template type parameter", TokenKind::SemanticType},
			{"concept", TokenKind::SemanticType},
			{"field", TokenKind::SemanticMember},
			{"enum constant", TokenKind::SemanticMember},
			{"instance property", TokenKind::SemanticMember},
			{"static property", TokenKind::SemanticMember},
			{"namespace", TokenKind::SemanticNamespace},
			{"macro", TokenKind::SemanticMacro},
			{"parameter", TokenKind::SemanticParameter},
		}};

		for (const auto& [kind, colour] : kKinds)
		{
			if (kind == hoverKind)
			{
				return colour;
			}
		}

		return TokenKind::Text;
	}

	std::vector<SemanticSpan> DeclarationSpans(std::string_view text, const Tokens& tokens,
											   std::string_view name, TokenKind nameKind)
	{
		std::vector<SemanticSpan> spans;

		if (name.starts_with('~'))
		{
			name.remove_prefix(1);
		}

		std::size_t nameIndex = 0;

		while (nameIndex < tokens.size() && !IsDeclaredName(text, tokens, nameIndex, name))
		{
			++nameIndex;
		}

		if (nameIndex == tokens.size())
		{
			return spans;
		}

		TypesIn(text, tokens, kFirstToken, nameIndex, nameIndex, spans);

		if (nameKind != TokenKind::Text)
		{
			AddSpan(spans, tokens[nameIndex], nameKind);
		}

		std::size_t openIndex = nameIndex + 1;

		if (IsPunctuation(text, tokens, openIndex, '<'))
		{
			std::size_t closeIndex = openIndex;

			for (int depth = 0; closeIndex < tokens.size(); ++closeIndex)
			{
				depth += Nesting(text, tokens, closeIndex);

				if (depth == 0)
				{
					break;
				}
			}

			TypesIn(text, tokens, openIndex + 1, closeIndex, closeIndex, spans);
			openIndex = closeIndex + 1;
		}

		if (!IsPunctuation(text, tokens, openIndex, '('))
		{
			return spans;
		}

		std::size_t parameterStart = openIndex + 1;
		std::size_t index = parameterStart;
		int depth = 1;

		for (; index < tokens.size(); ++index)
		{
			depth += Nesting(text, tokens, index);

			if (depth == 0 || (depth == 1 && IsPunctuation(text, tokens, index, ',')))
			{
				ParameterIn(text, tokens, parameterStart, index, spans);
				parameterStart = index + 1;
			}

			if (depth == 0)
			{
				break;
			}
		}

		for (++index; index + 1 < tokens.size(); ++index)
		{
			if (IsPunctuation(text, tokens, index, '-') &&
				IsPunctuation(text, tokens, index + 1, '>'))
			{
				TypesIn(text, tokens, index + kTokensInArrow, tokens.size(), tokens.size(), spans);
				break;
			}
		}

		return spans;
	}

	std::vector<SemanticSpan> TypeSpans(std::string_view text, const Tokens& tokens)
	{
		std::vector<SemanticSpan> spans;

		const std::size_t akaIndex = AkaIn(text, tokens, kFirstToken, tokens.size());
		TypesIn(text, tokens, kFirstToken, akaIndex, akaIndex, spans);
		AkaTypes(text, tokens, akaIndex, tokens.size(), spans);

		return spans;
	}

	std::vector<SemanticSpan> ParameterSpans(std::string_view text, const Tokens& tokens)
	{
		std::vector<SemanticSpan> spans;
		ParameterIn(text, tokens, kFirstToken, tokens.size(), spans);
		return spans;
	}
}
