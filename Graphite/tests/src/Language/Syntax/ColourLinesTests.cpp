#include <doctest/doctest.h>

#include "Language/Syntax/ColourLines.h"
#include "SyntaxFixtures.h"

#include <ostream>
#include <string>
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

TEST_CASE("An interpolation's braces are coloured as the string they sit in")
{
	const std::vector<std::string_view> lines{R"(var s = $"{name}!";)"};
	const std::vector<std::vector<Token>> tokens = ColourLines(LanguageId::CSharp, lines);
	const std::size_t opening = lines[0].find('{');
	const std::size_t closing = lines[0].find('}');

	REQUIRE(tokens.size() == 1);

	Graphite::TokenKind openingKind = Graphite::TokenKind::Text;
	Graphite::TokenKind closingKind = Graphite::TokenKind::Text;

	for (const Token& token : tokens[0])
	{
		if (token.From <= opening && opening < token.To)
		{
			openingKind = token.Kind;
		}

		if (token.From <= closing && closing < token.To)
		{
			closingKind = token.Kind;
		}
	}

	CHECK(openingKind == Graphite::TokenKind::String);
	CHECK(closingKind == Graphite::TokenKind::String);
}

namespace {

	std::string DescribeDeclaration(LanguageId language, std::string_view line)
	{
		const std::vector<std::string_view> lines{line};
		const std::vector<std::vector<Token>> tokens = Graphite::ColourDeclaration(language, lines);

		return tokens.size() == 1 ? DescribeTokens(line, tokens[0]) : "no single row";
	}
}

TEST_CASE("A C# declaration qualified by its type colours the type as a type and the method as a function")
{
	CHECK(DescribeDeclaration(LanguageId::CSharp, "void Console.WriteLine(string? value)") ==
		  "keyword 'void' | type 'Console' | punctuation '.' | function 'WriteLine' | punctuation '(' | keyword 'string' | "
		  "punctuation '?' | parameter 'value' | punctuation ')'");
}

TEST_CASE("A C# declaration qualified by a namespace and its type colours each part as what it names")
{
	CHECK(DescribeDeclaration(LanguageId::CSharp, "void System.Console.WriteLine(string? value)") ==
		  "keyword 'void' | namespace 'System' | punctuation '.' | type 'Console' | punctuation '.' | function 'WriteLine' | "
		  "punctuation '(' | keyword 'string' | punctuation '?' | parameter 'value' | punctuation ')'");

	CHECK(DescribeDeclaration(LanguageId::CSharp, "string System.IO.Path.Combine(string path1, string path2)") ==
		  "keyword 'string' | namespace 'System' | punctuation '.' | namespace 'IO' | punctuation '.' | type 'Path' | "
		  "punctuation '.' | function 'Combine' | punctuation '(' | keyword 'string' | parameter 'path1' | punctuation ',' | "
		  "keyword 'string' | parameter 'path2' | punctuation ')'");
}

TEST_CASE("A C# property declaration qualified by its type colours the property as a member")
{
	CHECK(DescribeDeclaration(LanguageId::CSharp, "int Widget.Size { get; set; }") ==
		  "keyword 'int' | type 'Widget' | punctuation '.' | member 'Size' | punctuation '{' | keyword 'get' | punctuation ';' | "
		  "keyword 'set' | punctuation ';' | punctuation '}'");
}

TEST_CASE("A C# declaration over several lines is coloured as one member")
{
	const std::vector<std::string_view> lines{"void Console.Write(", "    string format)"};
	const std::vector<std::vector<Token>> tokens = Graphite::ColourDeclaration(LanguageId::CSharp, lines);

	REQUIRE(tokens.size() == 2);
	CHECK(DescribeTokens(lines[0], tokens[0]) == "keyword 'void' | type 'Console' | punctuation '.' | function 'Write' | punctuation '('");
	CHECK(DescribeTokens(lines[1], tokens[1]) == "keyword 'string' | parameter 'format' | punctuation ')'");
}

TEST_CASE("A C# declaration of a type or a namespace is coloured where it stands")
{
	for (const std::string_view declaration : {"class Greeter", "namespace Demo.App", "enum Colour", "delegate void Handler(int code)"})
	{
		const std::vector<std::string_view> lines{declaration};

		CHECK(Graphite::ColourDeclaration(LanguageId::CSharp, lines) == ColourLines(LanguageId::CSharp, lines));
	}
}

TEST_CASE("A C++ declaration is coloured as ColourLines colours it")
{
	const std::vector<std::string_view> lines{"void Core::Layer::OnUpdate(float timestep) const override"};

	CHECK(Graphite::ColourDeclaration(LanguageId::Cpp, lines) == ColourLines(LanguageId::Cpp, lines));
	CHECK(Graphite::ColourDeclaration(LanguageId::PlainText, lines).empty());
}

TEST_CASE("An explicit interface implementation in C# code colours the interface as a type")
{
	const std::vector<std::string_view> lines{"class Widget : IDisposable { void IDisposable.Dispose() { } }"};
	const std::vector<std::vector<Token>> tokens = ColourLines(LanguageId::CSharp, lines);

	REQUIRE(tokens.size() == 1);
	CHECK(DescribeTokens(lines[0], tokens[0]).find("type 'IDisposable' | punctuation '.' | function 'Dispose'") != std::string::npos);
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
