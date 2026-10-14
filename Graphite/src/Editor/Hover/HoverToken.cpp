#include "Editor/Hover/HoverToken.h"

#include "Language/Server/LspProtocol.h"

#include "Core/Text/Columns.h"

#include <algorithm>

namespace Graphite {

	std::optional<HoverToken> TokenUnderByte(std::string_view rawLine, std::size_t lineNumber,
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

			const std::size_t clampedTo = std::min(token.To, rawLine.size());

			if (token.From >= clampedTo)
			{
				return std::nullopt;
			}

			return HoverToken{lineNumber, token.From, clampedTo};
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
		// WARNING: Goes through the column both lines share, not byte arithmetic: a multi-byte
		// character and a tab advance the two lines at different rates.
		const std::size_t sharedColumn = Core::ColumnOfByte(expandedLine, expandedByte, tabWidth);

		return Core::ByteOfColumn(rawLine, sharedColumn, tabWidth);
	}

	std::vector<Token> RawTokensOf(std::string_view rawLine, std::string_view expandedLine,
								   const std::vector<Token>& expandedTokens, std::size_t tabWidth)
	{
		std::vector<Token> rawTokens;
		rawTokens.reserve(expandedTokens.size());

		for (const Token& token : expandedTokens)
		{
			const std::size_t rawFrom = RawByteOfExpanded(rawLine, expandedLine, token.From, tabWidth);
			const std::size_t rawTo = RawByteOfExpanded(rawLine, expandedLine, token.To, tabWidth);

			rawTokens.push_back(Token{token.Kind, rawFrom, rawTo});
		}

		return rawTokens;
	}
}
