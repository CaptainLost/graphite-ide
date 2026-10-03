#include "Editor/HoverColouring.h"

#include <array>
#include <cstddef>
#include <utility>

namespace Graphite {

	namespace {

		using Tokens = std::vector<Token>;

		std::string_view TextOf(std::string_view text, const Token& token)
		{
			return text.substr(token.From, token.To - token.From);
		}

		bool IsPunctuation(std::string_view text, const Tokens& tokens, std::size_t index, char c)
		{
			return index < tokens.size() && tokens[index].Kind == TokenKind::Punctuation &&
				   text[tokens[index].From] == c;
		}

		// `std` in `std::string`. A class can qualify a name as well as a namespace can, but in what
		// clangd prints it is a namespace far more often than not, and a class qualifier is drawn
		// in a colour close to a type's anyway.
		bool Qualifies(std::string_view text, const Tokens& tokens, std::size_t index)
		{
			return IsPunctuation(text, tokens, index + 1, ':') &&
				   IsPunctuation(text, tokens, index + 2, ':');
		}

		void Add(std::vector<SemanticSpan>& spans, const Token& token, TokenKind kind)
		{
			spans.push_back({token.From, token.To, kind});
		}

		// How far `tokens[index]` nests the brackets it opens or closes: +1, -1, or 0 for any
		// other token. Angle brackets count too, since a parameter's type can be a template; a
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

		// Identifiers in [first, last) as types and their qualifiers as namespaces, leaving out
		// whatever stands inside square brackets — an array's extent is an expression.
		void TypesIn(std::string_view text, const Tokens& tokens, std::size_t first,
					 std::size_t last, std::size_t skip, std::vector<SemanticSpan>& spans)
		{
			int square = 0;

			for (std::size_t index = first; index < last; ++index)
			{
				if (IsPunctuation(text, tokens, index, '['))
				{
					++square;
				}
				else if (IsPunctuation(text, tokens, index, ']'))
				{
					--square;
				}
				else if (index != skip && square == 0 &&
						 tokens[index].Kind == TokenKind::Identifier)
				{
					Add(spans, tokens[index],
						Qualifies(text, tokens, index) ? TokenKind::SemanticNamespace
													   : TokenKind::SemanticType);
				}
			}
		}

		// Where clangd's " (aka std::size_t)" starts in [first, last), or `last` when it is not
		// there: the type spelled out, which follows whatever it is the spelling of.
		std::size_t AkaIn(std::string_view text, const Tokens& tokens, std::size_t first,
						  std::size_t last)
		{
			int depth = 0;

			for (std::size_t index = first; index + 1 < last; ++index)
			{
				if (depth == 0 && IsPunctuation(text, tokens, index, '(') &&
					tokens[index + 1].Kind == TokenKind::Identifier &&
					TextOf(text, tokens[index + 1]) == "aka")
				{
					return index;
				}

				depth += Nesting(text, tokens, index);
			}

			return last;
		}

		// The aka at [aka, last), if there is one: its type's identifiers coloured as types, the
		// word itself left as it is.
		void AkaTypes(std::string_view text, const Tokens& tokens, std::size_t aka,
					  std::size_t last, std::vector<SemanticSpan>& spans)
		{
			if (aka + 2 <= last)
			{
				TypesIn(text, tokens, aka + 2, last, last, spans);
			}
		}

		// One parameter, tokens [first, last): "const Event & event = {}".
		void ParameterIn(std::string_view text, const Tokens& tokens, std::size_t first,
						 std::size_t last, std::vector<SemanticSpan>& spans)
		{
			const std::size_t aka = AkaIn(text, tokens, first, last);
			AkaTypes(text, tokens, aka, last, spans);
			last = aka;

			// What follows a top-level "=" is a default argument, left to the lexer.
			std::size_t end = last;
			int depth = 0;

			for (std::size_t index = first; index < last; ++index)
			{
				depth += Nesting(text, tokens, index);

				if (depth == 0 && IsPunctuation(text, tokens, index, '='))
				{
					end = index;
					break;
				}
			}

			// The name is the declarator's last identifier, if it stands outside every bracket
			// and after something that spells a type: an unnamed `Event&` or `std::string` ends
			// in its type instead.
			std::size_t name = end;
			depth = 0;

			for (std::size_t index = first; index < end; ++index)
			{
				depth += Nesting(text, tokens, index);

				if (depth == 0 && tokens[index].Kind == TokenKind::Identifier)
				{
					name = index;
				}
			}

			if (name != end && name > first)
			{
				const Token& before = tokens[name - 1];

				const bool afterType = before.Kind == TokenKind::Identifier ||
									   before.Kind == TokenKind::Keyword ||
									   IsPunctuation(text, tokens, name - 1, '*') ||
									   IsPunctuation(text, tokens, name - 1, '&') ||
									   IsPunctuation(text, tokens, name - 1, '>');

				if (!afterType)
				{
					name = end;
				}
			}
			else
			{
				name = end;
			}

			if (name != end)
			{
				Add(spans, tokens[name], TokenKind::SemanticParameter);
			}

			TypesIn(text, tokens, first, end, name, spans);
		}
	}

	TokenKind KindOfHoverSymbol(std::string_view hoverKind)
	{
		constexpr std::array<std::pair<std::string_view, TokenKind>, 21> kKinds{{
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

		// A destructor is named "~Layer", which the lexer splits into a "~" and an identifier.
		if (name.starts_with('~'))
		{
			name.remove_prefix(1);
		}

		std::size_t at = 0;

		while (at < tokens.size() &&
			   !(tokens[at].Kind == TokenKind::Identifier && TextOf(text, tokens[at]) == name &&
				 !Qualifies(text, tokens, at)))
		{
			++at;
		}

		// No name to anchor on — an operator, whose name is no identifier — and no telling the
		// type from the rest.
		if (at == tokens.size())
		{
			return spans;
		}

		TypesIn(text, tokens, 0, at, at, spans);

		if (nameKind != TokenKind::Text)
		{
			Add(spans, tokens[at], nameKind);
		}

		std::size_t open = at + 1;

		// An explicit specialisation names its template arguments: "CreateScope<Layer>(...)".
		if (IsPunctuation(text, tokens, open, '<'))
		{
			std::size_t close = open;

			for (int depth = 0; close < tokens.size(); ++close)
			{
				depth += Nesting(text, tokens, close);

				if (depth == 0)
				{
					break;
				}
			}

			TypesIn(text, tokens, open + 1, close, close, spans);
			open = close + 1;
		}

		if (!IsPunctuation(text, tokens, open, '('))
		{
			return spans;
		}

		// The parameter list, split on the commas between parameters, not those of a template's
		// arguments inside one.
		std::size_t start = open + 1;
		std::size_t index = start;
		int depth = 1;

		for (; index < tokens.size(); ++index)
		{
			depth += Nesting(text, tokens, index);

			if (depth == 0 || (depth == 1 && IsPunctuation(text, tokens, index, ',')))
			{
				ParameterIn(text, tokens, start, index, spans);
				start = index + 1;
			}

			if (depth == 0)
			{
				break;
			}
		}

		// A trailing return type: "auto Size() const -> std::size_t".
		for (++index; index + 1 < tokens.size(); ++index)
		{
			if (IsPunctuation(text, tokens, index, '-') &&
				IsPunctuation(text, tokens, index + 1, '>'))
			{
				TypesIn(text, tokens, index + 2, tokens.size(), tokens.size(), spans);
				break;
			}
		}

		return spans;
	}

	std::vector<SemanticSpan> TypeSpans(std::string_view text, const Tokens& tokens)
	{
		std::vector<SemanticSpan> spans;

		const std::size_t aka = AkaIn(text, tokens, 0, tokens.size());
		TypesIn(text, tokens, 0, aka, aka, spans);
		AkaTypes(text, tokens, aka, tokens.size(), spans);

		return spans;
	}

	std::vector<SemanticSpan> ParameterSpans(std::string_view text, const Tokens& tokens)
	{
		std::vector<SemanticSpan> spans;
		ParameterIn(text, tokens, 0, tokens.size(), spans);
		return spans;
	}
}
