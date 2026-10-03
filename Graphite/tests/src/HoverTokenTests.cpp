#include <doctest/doctest.h>

#include "Editor/HoverToken.h"
#include "Language/Registry/LanguageRegistry.h"

#include "Core/Text/Columns.h"

using namespace Graphite;

namespace {
	std::vector<Token> Lex(std::string_view line)
	{
		std::vector<Token> tokens;
		LexerFor(LanguageId::Cpp)->LexLine(line, LineState{}, tokens);
		return tokens;
	}
}

TEST_CASE("The token under the mouse is taken only from Identifier tokens")
{
	const std::string line = "int x = 1; // foo";
	const auto tokens = Lex(line);

	CHECK(TokenUnderByte(line, 0, 4, tokens).has_value());		  // x
	CHECK_FALSE(TokenUnderByte(line, 0, 0, tokens).has_value());  // int, a keyword
	CHECK_FALSE(TokenUnderByte(line, 0, 14, tokens).has_value()); // foo, inside a comment
	CHECK_FALSE(TokenUnderByte(line, 0, 5, tokens).has_value());  // whitespace
}

TEST_CASE("The LSP column of a token after a tab counts the tab as one unit")
{
	const std::string line = "\tlayer->OnUpdate(ts);";
	const auto tokens = Lex(line);

	const auto token = TokenUnderByte(line, 7, 9, tokens); // inside OnUpdate (raw byte 8..16)
	REQUIRE(token.has_value());
	CHECK(token->From == 8);

	const FilePosition where = PositionOfToken("C:/p/a.cpp", line, *token);
	CHECK(where.Line == 7);
	CHECK(where.Column.Value == 8);
}

TEST_CASE("The LSP column counts UTF-16 units, not bytes")
{
	const std::string line = "auto \xC4\x85 = zz;"; // "ą" is two bytes and one UTF-16 unit
	const auto tokens = Lex(line);

	const auto token = TokenUnderByte(line, 0, 11, tokens); // zz
	REQUIRE(token.has_value());
	// "auto " is 5 units, "ą" 1, " = " 3: zz starts at UTF-16 column 9, raw byte 10.
	CHECK(PositionOfToken("C:/p/a.cpp", line, *token).Column.Value == 9);
}

TEST_CASE("Tokens of the tab-expanded line map back onto the raw line's bytes")
{
	const std::string raw = "\tlayer->OnUpdate(ts);";
	const std::string expanded = Core::ExpandTabs(raw, 4);
	const auto expandedTokens = Lex(expanded);

	const std::vector<Token> rawTokens = RawTokensOf(raw, expanded, expandedTokens, 4);
	REQUIRE(rawTokens.size() == expandedTokens.size());

	// OnUpdate sits at expanded bytes 11..19 and raw bytes 8..16.
	const std::size_t mouse = RawByteOfExpanded(raw, expanded, 13, 4);
	CHECK(mouse == 10);

	const auto token = TokenUnderByte(raw, 2, mouse, rawTokens);
	REQUIRE(token.has_value());
	CHECK(token->From == 8);
	CHECK(token->To == 16);
}

TEST_CASE("A multi-byte character before a tab still maps expanded bytes back to raw ones")
{
	// "ą" is two bytes and one column, so the tab after it expands to two spaces at width 4,
	// and the expanded line runs one byte ahead of the column count from there on.
	const std::string raw = "a\xC4\x85\tzz";
	const std::string expanded = Core::ExpandTabs(raw, 4);
	REQUIRE(expanded == "a\xC4\x85  zz");

	const auto expandedTokens = Lex(expanded);
	const std::vector<Token> rawTokens = RawTokensOf(raw, expanded, expandedTokens, 4);

	// zz is at expanded bytes 5..7 and raw bytes 4..6.
	const std::size_t mouse = RawByteOfExpanded(raw, expanded, 5, 4);
	CHECK(mouse == 4);

	const auto token = TokenUnderByte(raw, 0, mouse, rawTokens);
	REQUIRE(token.has_value());
	CHECK(token->From == 4);
	CHECK(token->To == 6);
}
