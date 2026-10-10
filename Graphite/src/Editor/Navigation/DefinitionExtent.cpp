#include "Editor/Navigation/DefinitionExtent.h"

#include <algorithm>
#include <optional>
#include <vector>

namespace Graphite {

	LineRange DefinitionExtent(std::span<const std::string_view> lines, std::span<const std::vector<Token>> tokens,
							   std::size_t firstLine)
	{
		LineRange range{firstLine, firstLine};

		int depth = 0;
		bool opened = false;
		std::size_t lastSeen = firstLine;
		std::optional<std::size_t> closedAt;

		const std::size_t scannedCount = std::min({lines.size(), tokens.size(), kDefinitionScanLines});

		for (std::size_t lineIndex = 0; lineIndex < scannedCount; ++lineIndex)
		{
			const std::size_t line = firstLine + lineIndex;
			const std::string_view text = lines[lineIndex];

			lastSeen = line;

			// The lexer is what says a brace is code: one inside a string, a character literal or
			// a comment is a different token kind and never reaches the counting below.
			for (const Token& token : tokens[lineIndex])
			{
				if (closedAt)
				{
					if (token.Kind == TokenKind::Comment)
					{
						continue;
					}

					const bool single =
						token.Kind == TokenKind::Punctuation && token.To - token.From == 1;
					const char next = single ? text[token.From] : '\0';

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

				const char punctuation = text[token.From];

				if (punctuation == '{')
				{
					++depth;
					opened = true;
				}
				else if (punctuation == '}')
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
				else if (punctuation == ';' && depth == 0)
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
		range.Last = std::min(firstLine + kDefinitionFallbackLines - 1, lastSeen);
		return range;
	}
}
