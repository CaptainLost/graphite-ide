#include <doctest/doctest.h>

#include "Editor/Hover/HoverToken.h"

#include "Core/Text/Columns.h"

#include "SyntaxFixtures.h"

#include <optional>
#include <string>
#include <vector>

using namespace Graphite;

TEST_CASE("The token under the mouse is taken only from names")
{
	const std::string line = "int x = 1; // foo";
	const std::vector<Token> tokens = LexCppLine(line);

	CHECK(TokenUnderByte(line, 0, 4, tokens).has_value());
	CHECK_FALSE(TokenUnderByte(line, 0, 0, tokens).has_value());
	CHECK_FALSE(TokenUnderByte(line, 0, 14, tokens).has_value());
	CHECK_FALSE(TokenUnderByte(line, 0, 5, tokens).has_value());
}

TEST_CASE("The LSP column of a token after a tab counts the tab as one unit")
{
	const std::string line = "\tlayer->OnUpdate(ts);";
	const std::vector<Token> tokens = LexCppLine(line);

	const std::optional<HoverToken> token = TokenUnderByte(line, 7, 9, tokens);
	REQUIRE(token.has_value());
	CHECK(token->From == 8);

	const FilePosition where = PositionOfToken("C:/p/a.cpp", line, *token);
	CHECK(where.Line == 7);
	CHECK(where.Column.Value == 8);
}

TEST_CASE("The LSP column counts UTF-16 units, not bytes")
{
	const std::string line = "auto \xC4\x85 = zz;";
	const std::vector<Token> tokens = LexCppLine(line);

	const std::optional<HoverToken> token = TokenUnderByte(line, 0, 11, tokens);
	REQUIRE(token.has_value());
	CHECK(PositionOfToken("C:/p/a.cpp", line, *token).Column.Value == 9);
}

TEST_CASE("Tokens of the tab-expanded line map back onto the raw line's bytes")
{
	const std::string raw = "\tlayer->OnUpdate(ts);";
	const std::string expanded = Core::ExpandTabs(raw, 4);
	const std::vector<Token> expandedTokens = LexCppLine(expanded);

	const std::vector<Token> rawTokens = RawTokensOf(raw, expanded, expandedTokens, 4);
	REQUIRE(rawTokens.size() == expandedTokens.size());

	const std::size_t mouse = RawByteOfExpanded(raw, expanded, 13, 4);
	CHECK(mouse == 10);

	const std::optional<HoverToken> token = TokenUnderByte(raw, 2, mouse, rawTokens);
	REQUIRE(token.has_value());
	CHECK(token->From == 8);
	CHECK(token->To == 16);
}

TEST_CASE("A multi-byte character before a tab still maps expanded bytes back to raw ones")
{
	const std::string raw = "a\xC4\x85\tzz";
	const std::string expanded = Core::ExpandTabs(raw, 4);
	REQUIRE(expanded == "a\xC4\x85  zz");

	const std::vector<Token> expandedTokens = LexCppLine(expanded);
	const std::vector<Token> rawTokens = RawTokensOf(raw, expanded, expandedTokens, 4);

	const std::size_t mouse = RawByteOfExpanded(raw, expanded, 5, 4);
	CHECK(mouse == 4);

	const std::optional<HoverToken> token = TokenUnderByte(raw, 0, mouse, rawTokens);
	REQUIRE(token.has_value());
	CHECK(token->From == 4);
	CHECK(token->To == 6);
}

TEST_CASE("The token under the mouse may be a name the grammar coloured as a type")
{
	const std::string line = "Widget w;";
	const std::vector<Token> tokens{Token{.Kind = TokenKind::SemanticType, .From = 0, .To = 6}};

	const std::optional<HoverToken> token = TokenUnderByte(line, 0, 2, tokens);

	REQUIRE(token.has_value());
	CHECK(token->From == 0);
	CHECK(token->To == 6);
}
