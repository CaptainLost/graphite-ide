#include <doctest/doctest.h>

#include "Language/Syntax/ColourLines.h"
#include "SyntaxFixtures.h"

#include <string_view>
#include <vector>

using Graphite::ColourLines;
using Graphite::LanguageId;
using Graphite::Token;

TEST_CASE("ColourLines carries a C++ block comment from one line into the next")
{
	const std::vector<std::string_view> lines{"/* opened", "still inside */ int x;"};
	const std::vector<std::vector<Token>> tokens = ColourLines(LanguageId::Cpp, lines);

	REQUIRE(tokens.size() == 2);
	CHECK(DescribeTokens(lines[0], tokens[0]) == "comment '/* opened'");
	CHECK(DescribeTokens(lines[1], tokens[1]).starts_with("comment 'still inside */' | keyword 'int'"));
}

TEST_CASE("ColourLines parses C# lines as one text, so a comment over two lines colours both")
{
	const std::vector<std::string_view> lines{"/* multi", "   line */ int x = 1;"};
	const std::vector<std::vector<Token>> tokens = ColourLines(LanguageId::CSharp, lines);

	REQUIRE(tokens.size() == 2);
	CHECK(DescribeTokens(lines[0], tokens[0]) == "comment '/* multi'");
	CHECK(DescribeTokens(lines[1], tokens[1]) == "comment '   line */' | keyword 'int' | identifier 'x' | punctuation '=' | number '1' | punctuation ';'");
}

TEST_CASE("ColourLines colours a raw string that spans C# lines")
{
	std::vector<std::string_view> lines;

	for (std::size_t row = 13; row <= 15; ++row)
	{
		lines.push_back(FixtureRow(kCSharpFixture, row));
	}

	const std::vector<std::vector<Token>> tokens = ColourLines(LanguageId::CSharp, lines);

	REQUIRE(tokens.size() == 3);
	CHECK(DescribeTokens(lines[0], tokens[0]) == "keyword 'var' | identifier 'raw' | punctuation '=' | string '\"\"\"'");
	CHECK(DescribeTokens(lines[1], tokens[1]) == "string '          raw \"text\"'");
	CHECK(DescribeTokens(lines[2], tokens[2]) == "string '          \"\"\"' | punctuation ';'");
}

TEST_CASE("ColourLines colours nothing for a language with no syntax or for no lines")
{
	const std::vector<std::string_view> lines{"plain text"};
	const std::vector<std::string_view> noLines;

	CHECK(ColourLines(LanguageId::PlainText, lines).empty());
	CHECK(ColourLines(LanguageId::TypeScript, lines).empty());
	CHECK(ColourLines(LanguageId::CSharp, noLines).empty());
	CHECK(ColourLines(LanguageId::Cpp, noLines).empty());
}
