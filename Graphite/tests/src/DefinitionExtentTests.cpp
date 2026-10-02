#include <doctest/doctest.h>

#include "Editor/DefinitionExtent.h"
#include "Lang/Registry/LanguageRegistry.h"

#include <string>
#include <vector>

using Graphite::DefinitionExtent;
using Graphite::LineRange;

namespace {
	LineRange ExtentOf(const std::vector<std::string>& lines, std::size_t start)
	{
		const Graphite::Lexer* lexer = Graphite::LexerFor(Graphite::LanguageId::Cpp);
		REQUIRE(lexer != nullptr);

		return DefinitionExtent(
			[&lines](std::size_t i) -> std::optional<std::string_view> {
				if (i >= lines.size())
					return std::nullopt;
				return std::string_view(lines[i]);
			},
			start, *lexer);
	}
}

TEST_CASE("A function body ends at its matching brace")
{
	const std::vector<std::string> lines{"int Sum(int a, int b)", "{", "    return a + b;", "}", "int Next;"};
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
	const std::vector<std::string> lines{"void F() {", "  const char* s = \"}\"; // }", "  /* } */", "}"};

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
		lines.push_back("  x();");

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
