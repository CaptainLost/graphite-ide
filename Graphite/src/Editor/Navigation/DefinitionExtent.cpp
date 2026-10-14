#include "Editor/Navigation/DefinitionExtent.h"

#include <algorithm>
#include <optional>
#include <vector>

namespace Graphite {

	namespace {

		bool IsSingleCharacterPunctuation(const Token& token)
		{
			return token.Kind == TokenKind::Punctuation && token.To - token.From == 1;
		}
	}

	DefinitionLineRange DefinitionExtent(std::span<const std::string_view> linesFromFirstLine,
										 std::span<const std::vector<Token>> tokensPerLine, std::size_t firstLine)
	{
		DefinitionLineRange range{firstLine, firstLine};

		int braceDepth = 0;
		bool hasOpenedBrace = false;
		std::size_t lastScannedLine = firstLine;
		std::optional<std::size_t> braceClosedAtLine;

		const std::size_t scannedCount =
			std::min({linesFromFirstLine.size(), tokensPerLine.size(), kDefinitionScanLines});

		for (std::size_t lineIndex = 0; lineIndex < scannedCount; ++lineIndex)
		{
			const std::size_t line = firstLine + lineIndex;
			const std::string_view text = linesFromFirstLine[lineIndex];

			lastScannedLine = line;

			for (const Token& token : tokensPerLine[lineIndex])
			{
				if (braceClosedAtLine)
				{
					if (token.Kind == TokenKind::Comment)
					{
						continue;
					}

					const char nextPunctuation = IsSingleCharacterPunctuation(token) ? text[token.From] : '\0';
					const bool continuesDeclaration = nextPunctuation == ',' || nextPunctuation == '{';

					if (!continuesDeclaration)
					{
						range.Last = *braceClosedAtLine;
						return range;
					}

					braceClosedAtLine.reset();
				}

				if (!IsSingleCharacterPunctuation(token))
				{
					continue;
				}

				const char punctuation = text[token.From];

				if (punctuation == '{')
				{
					++braceDepth;
					hasOpenedBrace = true;
				}
				else if (punctuation == '}')
				{
					--braceDepth;

					if (hasOpenedBrace && braceDepth == 0)
					{
						braceClosedAtLine = line;
					}
				}
				else if (punctuation == ';' && braceDepth == 0)
				{
					range.Last = line;
					return range;
				}
			}
		}

		if (braceClosedAtLine)
		{
			range.Last = *braceClosedAtLine;
			return range;
		}

		range.Last = std::min(firstLine + kDefinitionFallbackLines - 1, lastScannedLine);
		return range;
	}
}
