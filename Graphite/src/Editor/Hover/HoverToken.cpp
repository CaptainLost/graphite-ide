#include "Editor/Hover/HoverToken.h"

#include "Language/Server/LspProtocol.h"

#include "Core/Text/Columns.h"

#include <algorithm>

namespace Graphite {

	std::optional<HoverToken> TokenUnderByte(std::string_view rawLine, std::size_t line,
											 std::size_t rawByte,
											 const std::vector<Token>& rawTokens)
	{
		for (const Token& token : rawTokens)
		{
			if (!NamesSomething(token.Kind))
			{
				continue;
			}

			if (rawByte < token.From || rawByte >= token.To)
			{
				continue;
			}

			// Clamped rather than trusted: a token list from another revision of the line could
			// otherwise name bytes this line does not have.
			const std::size_t to = std::min(token.To, rawLine.size());

			if (token.From >= to)
			{
				return std::nullopt;
			}

			return HoverToken{line, token.From, to};
		}

		return std::nullopt;
	}

	FilePosition PositionOfToken(const std::filesystem::path& path, std::string_view rawLine,
								 const HoverToken& token)
	{
		return FilePosition{path, token.Line, ToUtf16Column(rawLine, token.From)};
	}

	std::size_t RawByteOfExpanded(std::string_view rawLine, std::string_view expandedLine,
								  std::size_t expandedByte, std::size_t tabWidth)
	{
		// Through the column both lines share: the expanded line holds no tabs, so its column of
		// a byte is a count of characters, and the raw line's byte of that column walks its tabs.
		// Not byte arithmetic, because a multi-byte character and a tab advance the two lines at
		// different rates.
		return Core::ByteOfColumn(rawLine, Core::ColumnOfByte(expandedLine, expandedByte, tabWidth),
								  tabWidth);
	}

	std::vector<Token> RawTokensOf(std::string_view rawLine, std::string_view expandedLine,
								   const std::vector<Token>& expandedTokens, std::size_t tabWidth)
	{
		std::vector<Token> raw;
		raw.reserve(expandedTokens.size());

		for (const Token& token : expandedTokens)
		{
			raw.push_back(Token{token.Kind,
								RawByteOfExpanded(rawLine, expandedLine, token.From, tabWidth),
								RawByteOfExpanded(rawLine, expandedLine, token.To, tabWidth)});
		}

		return raw;
	}
}
