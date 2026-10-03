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

	// The token a hover asks about, in the raw line's terms. The editor's syntax cache lexes the
	// tab-expanded line, so its offsets differ from the raw line's after a tab and after any
	// multi-byte character before one; the server is asked in raw-line terms, so everything a
	// hover keeps or sends is converted to them first, here.

	// The identifier token whose raw byte range holds `rawByte`, or nothing. Only Identifier
	// tokens count: a word in a comment or a string is not a symbol.
	std::optional<HoverToken> TokenUnderByte(std::string_view rawLine, std::size_t line,
											 std::size_t rawByte,
											 const std::vector<Token>& rawTokens);

	// The LSP position of a token's start: line, and the UTF-16 column of its raw byte offset.
	FilePosition PositionOfToken(const std::filesystem::path& path, std::string_view rawLine,
								 const HoverToken& token);

	// The raw byte that `expandedByte` of `expandedLine` — `rawLine` with tabs expanded at
	// `tabWidth` — came from. A byte inside a tab's run of spaces resolves to the tab.
	std::size_t RawByteOfExpanded(std::string_view rawLine, std::string_view expandedLine,
								  std::size_t expandedByte, std::size_t tabWidth);

	// The same tokens, with their ranges moved from the expanded line onto the raw one. Kinds
	// are kept, so a word inside a block comment that began lines above is still a comment —
	// which lexing the raw line on its own, from a default state, would get wrong.
	std::vector<Token> RawTokensOf(std::string_view rawLine, std::string_view expandedLine,
								   const std::vector<Token>& expandedTokens, std::size_t tabWidth);
}
