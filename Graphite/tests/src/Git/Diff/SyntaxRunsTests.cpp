#include <doctest/doctest.h>

#include "Git/Diff/SyntaxRuns.h"
#include "SyntaxFixtures.h"

#include <optional>
#include <string>
#include <vector>

TEST_CASE("ColourFileText lexes a one-line text into one line of tokens")
{
	const auto lines = Graphite::ColourFileText("int x;", "main.cpp");

	REQUIRE(lines.size() == 1);
	CHECK_FALSE(lines[0].empty());
}

TEST_CASE("ColourFileText lexes three lines whether or not the text ends with a newline")
{
	const auto withoutTrailingNewline = Graphite::ColourFileText("int a;\nint b;\nint c;", "main.cpp");
	const auto withTrailingNewline = Graphite::ColourFileText("int a;\nint b;\nint c;\n", "main.cpp");

	CHECK(withoutTrailingNewline.size() == 3);
	CHECK(withTrailingNewline.size() == 3);
}

TEST_CASE("ColourFileText yields no lines for an empty text")
{
	const auto lines = Graphite::ColourFileText("", "main.cpp");

	CHECK(lines.empty());
}

TEST_CASE("ColourFileText carries a block comment opened on the first line to the third")
{
	const auto lines = Graphite::ColourFileText("/* opened here\nstill inside\n*/ int x;", "main.cpp");

	REQUIRE(lines.size() == 3);
	REQUIRE_FALSE(lines[2].empty());

	// Lexing the third line on its own, with no entry state, would call "*/ int x;" a stray
	// */ followed by code — this is exactly the case the whole plan exists to get right.
	CHECK(lines[2][0].Kind == Graphite::TokenKind::Comment);
}

TEST_CASE("ColourFileText's token offsets are byte offsets into the raw line, tabs included")
{
	// A tab before "int" would be expanded to several columns by the editor's own lexing path,
	// but this view draws the raw line, so the token's From must count the tab as one byte.
	const auto lines = Graphite::ColourFileText("\tint x;", "main.cpp");

	REQUIRE(lines.size() == 1);
	REQUIRE_FALSE(lines[0].empty());
	CHECK(lines[0][0].From == 1);
	CHECK(lines[0][0].Kind == Graphite::TokenKind::Keyword);
}

TEST_CASE("ColourFileText leaves no stray carriage return inside a line's last token")
{
	const auto lines = Graphite::ColourFileText("// a comment\r\nint x;\r\n", "main.cpp");

	REQUIRE(lines.size() == 2);
	REQUIRE_FALSE(lines[0].empty());

	const Graphite::Token& lastOfFirstLine = lines[0].back();
	CHECK(lastOfFirstLine.To == 12); // "// a comment" is 12 bytes, no trailing '\r'
}

TEST_CASE("ColourFileText returns no lines for a path with no syntax")
{
	const auto lines = Graphite::ColourFileText("some text\nmore text", "README.md");

	CHECK(lines.empty());
}

TEST_CASE("StripLineTerminator takes one CRLF and no more")
{
	CHECK(Graphite::StripLineTerminator("line\r\n") == "line");
	CHECK(Graphite::StripLineTerminator("line\n") == "line");
	CHECK(Graphite::StripLineTerminator("line\r") == "line");
	CHECK(Graphite::StripLineTerminator("line") == "line");

	// The one case that told the two old copies of this rule apart: a doubled carriage
	// return is content, not a second terminator, and both callers now agree it is.
	CHECK(Graphite::StripLineTerminator("line\r\r\n") == "line\r");
	CHECK(Graphite::StripLineTerminator("").empty());
	CHECK(Graphite::StripLineTerminator("\n").empty());
}

TEST_CASE("A line with no tokens draws as one plain run")
{
	const auto runs = Graphite::RunsFor("plain text", nullptr);

	REQUIRE(runs.size() == 1);
	CHECK(runs.front() == Graphite::SyntaxRun{0, 10, std::nullopt});
}

TEST_CASE("An empty token vector draws as one plain run")
{
	const std::vector<Graphite::Token> tokens;
	const auto runs = Graphite::RunsFor("plain text", &tokens);

	REQUIRE(runs.size() == 1);
	CHECK_FALSE(runs.front().Kind.has_value());
}

TEST_CASE("The gap before a token and the tail after the last one are plain")
{
	const std::vector<Graphite::Token> tokens{Graphite::Token{Graphite::TokenKind::Keyword, 2, 5}};
	const auto runs = Graphite::RunsFor("  int x;", &tokens);

	REQUIRE(runs.size() == 3);
	CHECK(runs[0] == Graphite::SyntaxRun{0, 2, std::nullopt});
	CHECK(runs[1] == Graphite::SyntaxRun{2, 5, Graphite::TokenKind::Keyword});
	CHECK(runs[2] == Graphite::SyntaxRun{5, 8, std::nullopt});
}

TEST_CASE("A token reaching exactly the end of the line leaves no tail run")
{
	const std::vector<Graphite::Token> tokens{Graphite::Token{Graphite::TokenKind::Keyword, 0, 3}};
	const auto runs = Graphite::RunsFor("int", &tokens);

	REQUIRE(runs.size() == 1);
	CHECK(runs.front() == Graphite::SyntaxRun{0, 3, Graphite::TokenKind::Keyword});
}

TEST_CASE("A token reaching past the end of the line is clamped to it")
{
	const std::vector<Graphite::Token> tokens{Graphite::Token{Graphite::TokenKind::String, 0, 99}};
	const auto runs = Graphite::RunsFor("abc", &tokens);

	// A mismatch between the tokens and the line they were supposedly lexed from becomes
	// truncated colouring, not a read past the end of the line mid-frame.
	REQUIRE(runs.size() == 1);
	CHECK(runs.front() == Graphite::SyntaxRun{0, 3, Graphite::TokenKind::String});
}

TEST_CASE("An empty line with a token vector still draws one run")
{
	const std::vector<Graphite::Token> tokens{Graphite::Token{Graphite::TokenKind::Comment, 0, 4}};
	const auto runs = Graphite::RunsFor("", &tokens);

	// A row that draws no run at all sizes no item, so the cursor never advances and the
	// next row lands on top of it. One empty run is what keeps the row's height.
	REQUIRE(runs.size() == 1);
	CHECK(runs.front() == Graphite::SyntaxRun{0, 0, std::nullopt});
}

TEST_CASE("Tokens that all cover nothing still leave one run for the line")
{
	const std::vector<Graphite::Token> tokens{Graphite::Token{Graphite::TokenKind::Comment, 0, 0},
											  Graphite::Token{Graphite::TokenKind::Comment, 0, 0}};
	const auto runs = Graphite::RunsFor("abc", &tokens);

	REQUIRE_FALSE(runs.empty());
	CHECK(runs.back().To == 3);
}

TEST_CASE("ColourFileText colours a C# text, a comment over two lines on both")
{
	const std::vector<std::vector<Graphite::Token>> lines = Graphite::ColourFileText("/* a\nb */ int x;\n", "Program.cs");

	REQUIRE(lines.size() == 2);
	CHECK(DescribeTokens("/* a", lines[0]) == "comment '/* a'");
	CHECK(DescribeTokens("b */ int x;", lines[1]) == "comment 'b */' | keyword 'int' | identifier 'x' | punctuation ';'");
}
