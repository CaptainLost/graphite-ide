#include "Editor/DefinitionExtent.h"

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

		for (std::size_t line = startLine; line < startLine + kDefinitionScanLines; ++line)
		{
			const std::optional<std::string_view> text = lineAt(line);

			if (!text)
				break;

			lastSeen = line;
			tokens.clear();
			state = lexer.LexLine(*text, state, tokens);

			// The lexer is what says a brace is code: one inside a string, a character literal or
			// a comment is a different token kind and never reaches the counting below.
			for (const Token& token : tokens)
			{
				if (token.Kind != TokenKind::Punctuation || token.To - token.From != 1)
					continue;

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
						range.Last = line;

						// "};" closes a class on the same line; a lone "}" followed by ";" on the
						// next line is rare enough to leave out.
						return range;
					}
				}
				else if (c == ';' && !opened && depth == 0)
				{
					range.Last = line;
					return range;
				}
			}
		}

		// No end found within the scan, or the text ran out: show a screenful rather than nothing.
		range.Last = std::min(startLine + kDefinitionFallbackLines - 1, lastSeen);
		return range;
	}
}
