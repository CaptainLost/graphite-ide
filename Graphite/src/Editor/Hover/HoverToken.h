#pragma once

#include "Editor/Hover/HoverTracker.h"

#include "Language/Api/LanguageService.h"
#include "Language/Syntax/Token.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace Graphite {

	std::optional<HoverToken> TokenUnderByte(std::string_view rawLine, std::size_t lineNumber,
											 std::size_t rawByte,
											 const std::vector<Token>& rawTokens);

	FilePosition PositionOfToken(const std::filesystem::path& path, std::string_view rawLine,
								 const HoverToken& token);

	std::size_t RawByteOfExpanded(std::string_view rawLine, std::string_view expandedLine,
								  std::size_t expandedByte, std::size_t tabWidth);

	std::vector<Token> RawTokensOf(std::string_view rawLine, std::string_view expandedLine,
								   const std::vector<Token>& expandedTokens, std::size_t tabWidth);
}
