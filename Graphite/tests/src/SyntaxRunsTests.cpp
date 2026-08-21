#include <doctest/doctest.h>

#include "Git/SyntaxRuns.h"

#include <string>
#include <vector>

TEST_CASE("LexFileText lexes a one-line text into one line of tokens")
{
	const auto lines = Graphite::LexFileText("int x;", "main.cpp");

	REQUIRE(lines.size() == 1);
	CHECK_FALSE(lines[0].empty());
}

TEST_CASE("LexFileText lexes three lines whether or not the text ends with a newline")
{
	const auto withoutTrailingNewline = Graphite::LexFileText("int a;\nint b;\nint c;", "main.cpp");
	const auto withTrailingNewline = Graphite::LexFileText("int a;\nint b;\nint c;\n", "main.cpp");

	CHECK(withoutTrailingNewline.size() == 3);
	CHECK(withTrailingNewline.size() == 3);
}

TEST_CASE("LexFileText yields no lines for an empty text")
{
	const auto lines = Graphite::LexFileText("", "main.cpp");

	CHECK(lines.empty());
}

TEST_CASE("LexFileText carries a block comment opened on the first line to the third")
{
	const auto lines = Graphite::LexFileText("/* opened here\nstill inside\n*/ int x;", "main.cpp");

	REQUIRE(lines.size() == 3);
	REQUIRE_FALSE(lines[2].empty());

	// Lexing the third line on its own, with no entry state, would call "*/ int x;" a stray
	// */ followed by code — this is exactly the case the whole plan exists to get right.
	CHECK(lines[2][0].Kind == Graphite::TokenKind::Comment);
}

TEST_CASE("LexFileText's token offsets are byte offsets into the raw line, tabs included")
{
	// A tab before "int" would be expanded to several columns by the editor's own lexing path,
	// but this view draws the raw line, so the token's From must count the tab as one byte.
	const auto lines = Graphite::LexFileText("\tint x;", "main.cpp");

	REQUIRE(lines.size() == 1);
	REQUIRE_FALSE(lines[0].empty());
	CHECK(lines[0][0].From == 1);
	CHECK(lines[0][0].Kind == Graphite::TokenKind::Keyword);
}

TEST_CASE("LexFileText leaves no stray carriage return inside a line's last token")
{
	const auto lines = Graphite::LexFileText("// a comment\r\nint x;\r\n", "main.cpp");

	REQUIRE(lines.size() == 2);
	REQUIRE_FALSE(lines[0].empty());

	const Graphite::Token& lastOfFirstLine = lines[0].back();
	CHECK(lastOfFirstLine.To == 12); // "// a comment" is 12 bytes, no trailing '\r'
}

TEST_CASE("LexFileText returns no lines for a path with no lexer")
{
	const auto lines = Graphite::LexFileText("some text\nmore text", "README.md");

	CHECK(lines.empty());
}
