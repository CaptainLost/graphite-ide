#include "Editor/Navigation/DefinitionExtent.h"

#include <algorithm>
#include <vector>

namespace Graphite {

	LineRange DefinitionExtent(
		const std::function<std::optional<std::string_view>(std::size_t)>& lineAt,
		std::size_t startLine, const Lexer& lexer)
	{
		LineRange range{startLine, startLine};

		LineState state;
		std::vector<Token> tokens;
		int depth = 0;
		bool opened = false;
		std::size_t lastSeen = startLine;
		std::optional<std::size_t> closedAt;

		for (std::size_t line = startLine; line < startLine + kDefinitionScanLines; ++line)
		{
			const std::optional<std::string_view> text = lineAt(line);

			if (!text)
			{
				break;
			}

			lastSeen = line;
			tokens.clear();
			state = lexer.LexLine(*text, state, tokens);

			// The lexer is what says a brace is code: one inside a string, a character literal or
			// a comment is a different token kind and never reaches the counting below.
			for (const Token& token : tokens)
			{
				if (closedAt)
				{
					if (token.Kind == TokenKind::Comment)
					{
						continue;
					}

					const bool single =
						token.Kind == TokenKind::Punctuation && token.To - token.From == 1;
					const char next = single ? (*text)[token.From] : '\0';

					// A brace group closing at depth 0 is the end of the definition unless a
					// member initialiser follows: "a{1}, b{2} {" continues with another member or
					// the body.
					if (next != ',' && next != '{')
					{
						range.Last = *closedAt;
						return range;
					}

					closedAt.reset();
				}

				if (token.Kind != TokenKind::Punctuation || token.To - token.From != 1)
				{
					continue;
				}

				const char c = (*text)[token.From];

				if (c == '{')
				{
					++depth;
					opened = true;
				}
				else if (c == '}')
				{
					--depth;

					if (opened && depth == 0)
					{
						// The next significant token decides, possibly on a later line; "};"
						// ends on this line because the ";" is neither a comma nor an opening
						// brace.
						closedAt = line;
					}
				}
				else if (c == ';' && depth == 0)
				{
					range.Last = line;
					return range;
				}
			}
		}

		// The text or the scan ended right after a closing brace: that brace is the end.
		if (closedAt)
		{
			range.Last = *closedAt;
			return range;
		}

		// No end found within the scan, or the text ran out: show a screenful rather than nothing.
		range.Last = std::min(startLine + kDefinitionFallbackLines - 1, lastSeen);
		return range;
	}
}
