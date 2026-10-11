#include <doctest/doctest.h>

#include "Editor/Navigation/DefinitionExtent.h"
#include "Language/Registry/LanguageRegistry.h"
#include "Language/Syntax/ColourLines.h"

#include <string>
#include <string_view>
#include <vector>

using Graphite::DefinitionExtent;
using Graphite::LineRange;

namespace {
	LineRange ExtentIn(Graphite::LanguageId language, const std::vector<std::string>& lines, std::size_t start)
	{
		std::vector<std::string_view> scanned;

		for (std::size_t lineIndex = start; lineIndex < lines.size(); ++lineIndex)
		{
			scanned.push_back(lines[lineIndex]);
		}

		const std::vector<std::vector<Graphite::Token>> tokens = Graphite::ColourLines(language, scanned);
		REQUIRE_FALSE(tokens.empty());

		return DefinitionExtent(scanned, tokens, start);
	}

	LineRange ExtentOf(const std::vector<std::string>& lines, std::size_t start)
	{
		return ExtentIn(Graphite::LanguageId::Cpp, lines, start);
	}
}

TEST_CASE("A function body ends at its matching brace")
{
	const std::vector<std::string> lines{"int Sum(int a, int b)", "{", "    return a + b;", "}",
										 "int Next;"};
	const LineRange range = ExtentOf(lines, 0);

	CHECK(range.First == 0);
	CHECK(range.Last == 3);
}

TEST_CASE("Nested braces are counted")
{
	const std::vector<std::string> lines{"void F() {", "  if (x) {", "  }", "}", "}"};

	CHECK(ExtentOf(lines, 0).Last == 3);
}

TEST_CASE("A class ends at its closing brace and semicolon line")
{
	const std::vector<std::string> lines{"class Layer", "{", "public:", "  void F();", "};"};

	CHECK(ExtentOf(lines, 0).Last == 4);
}

TEST_CASE("Braces inside strings and comments are not counted")
{
	const std::vector<std::string> lines{"void F() {", "  const char* s = \"}\"; // }",
										 "  /* } */", "}"};

	CHECK(ExtentOf(lines, 0).Last == 3);
}

TEST_CASE("A declaration ends at its semicolon")
{
	CHECK(ExtentOf({"virtual void OnUpdate(Timestep ts) = 0;", "int x;"}, 0).Last == 0);
	CHECK(ExtentOf({"Layer() = default;", "{"}, 0).Last == 0);
	CHECK(ExtentOf({"void F(int a,", "       int b);", "{"}, 0).Last == 1);
}

TEST_CASE("An unbalanced brace falls back to twenty lines")
{
	std::vector<std::string> lines{"void F() {"};

	for (int i = 0; i < 30; ++i)
	{
		lines.push_back("  x();");
	}

	const LineRange range = ExtentOf(lines, 0);

	CHECK(range.First == 0);
	CHECK(range.Last == 19);
}

TEST_CASE("The fallback is cut short at the end of the text")
{
	const LineRange range = ExtentOf({"void F() {", "  x();"}, 0);

	CHECK(range.Last == 1);
}

TEST_CASE("A start on the last line is that line alone")
{
	CHECK(ExtentOf({"int a;", "int b"}, 1).Last == 1);
}

TEST_CASE("A constructor's member initialiser list does not end the extent")
{
	const std::vector<std::string> lines{"Foo() : a{1}, b{2}", "{", "    x();", "}", "int Next;"};

	CHECK(ExtentOf(lines, 0).Last == 3);
}

TEST_CASE("An initialiser list with the body brace on the same line")
{
	const std::vector<std::string> lines{"Foo() : a{1}, b{2} {", "    x();", "}", "int Next;"};

	CHECK(ExtentOf(lines, 0).Last == 2);
}

TEST_CASE("A multi-line block comment holding a brace is not counted")
{
	const std::vector<std::string> lines{"void F() {", "/* {", "} */", "}"};

	CHECK(ExtentOf(lines, 0).Last == 3);
}

TEST_CASE("A raw string holding braces is not counted")
{
	const std::vector<std::string> lines{"void F() {", "auto s = R\"(}{)\";", "}"};

	CHECK(ExtentOf(lines, 0).Last == 2);
}

TEST_CASE("An enum ends at its semicolon on the same line")
{
	CHECK(ExtentOf({"enum class Mode { Fast, Slow };"}, 0).Last == 0);
}

TEST_CASE("A deleted special member ends at its semicolon")
{
	CHECK(ExtentOf({"Foo(const Foo&) = delete;", "int x;"}, 0).Last == 0);
}

TEST_CASE("An expression-bodied member with an interpolation ends at its semicolon")
{
	const std::vector<std::string> lines{R"(public string Describe() => $"{Name} is ")", "    + Age.ToString();", "public int Age { get; }"};

	const LineRange range = ExtentIn(Graphite::LanguageId::CSharp, lines, 0);

	CHECK(range.First == 0);
	CHECK(range.Last == 1);
}

TEST_CASE("A C# class ends at its matching brace")
{
	const std::vector<std::string> lines{"public class Widget", "{", "    public int Count { get; set; }", "}", "class Next { }"};
	const LineRange range = ExtentIn(Graphite::LanguageId::CSharp, lines, 0);

	CHECK(range.First == 0);
	CHECK(range.Last == 3);
}
