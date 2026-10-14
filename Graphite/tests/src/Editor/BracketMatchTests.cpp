#include <doctest/doctest.h>

#include "Core/Text/TextBuffer.h"
#include "Editor/BracketMatch.h"
#include "Language/Registry/LanguageRegistry.h"
#include "Language/Syntax/SyntaxCache.h"

#include <optional>
#include <string>
#include <string_view>

namespace {

	struct Fixture
	{
		Core::TextBuffer Buffer;
		Graphite::SyntaxCache Syntax;

		explicit Fixture(std::string_view text) : Buffer(std::string(text))
		{
			Syntax.SetSyntax(Graphite::SyntaxOf(Graphite::LanguageId::Cpp));
		}

		std::optional<Graphite::BracketPair> At(std::size_t caret)
		{
			return Graphite::FindBracketPair(Buffer, Syntax, caret);
		}
	};
}

TEST_CASE("The bracket after the caret is matched forwards")
{
	Fixture fixture("int f(int a);");

	const std::optional<Graphite::BracketPair> pair = fixture.At(5);

	REQUIRE(pair.has_value());
	CHECK(pair->Matched);
	CHECK(pair->Open == 5);
	CHECK(pair->Close == 11);
}

TEST_CASE("With no bracket after it, the caret matches the one before it")
{
	Fixture fixture("int f(int a);");

	const std::optional<Graphite::BracketPair> pair = fixture.At(12);

	REQUIRE(pair.has_value());
	CHECK(pair->Matched);
	CHECK(pair->Open == 5);
	CHECK(pair->Close == 11);
}

TEST_CASE("Nesting is counted, not merely the next bracket of the same shape")
{
	Fixture fixture("f(g(x), y)");

	const std::optional<Graphite::BracketPair> pair = fixture.At(1);

	REQUIRE(pair.has_value());
	CHECK(pair->Matched);
	CHECK(pair->Open == 1);
	CHECK(pair->Close == 9);
}

TEST_CASE("A bracket with no partner comes back unmatched")
{
	Fixture fixture("f(x;");

	const std::optional<Graphite::BracketPair> pair = fixture.At(1);

	REQUIRE(pair.has_value());
	CHECK_FALSE(pair->Matched);
	CHECK(pair->Open == 1);
	CHECK(pair->Close == 1);
}

TEST_CASE("A bracket inside a string is not a bracket")
{
	Fixture fixture("const char* s = \"(\";");

	CHECK_FALSE(fixture.At(17).has_value());
}

TEST_CASE("A bracket inside a comment is not a bracket")
{
	Fixture fixture("// (\nint x;");

	CHECK_FALSE(fixture.At(3).has_value());
}

TEST_CASE("A bracket in a string does not close one in code")
{
	Fixture fixture("f(\")\");");

	const std::optional<Graphite::BracketPair> pair = fixture.At(1);

	REQUIRE(pair.has_value());
	CHECK(pair->Matched);
	CHECK(pair->Open == 1);
	CHECK(pair->Close == 5);
}

TEST_CASE("A multi-byte character before the bracket does not move it out of code")
{
	Fixture fixture("f(\"ą\");");

	const std::optional<Graphite::BracketPair> pair = fixture.At(1);

	REQUIRE(pair.has_value());
	CHECK(pair->Matched);
	CHECK(pair->Open == 1);
	CHECK(pair->Close == 6);
}

TEST_CASE("A pair spanning several lines is found")
{
	Fixture fixture("void f()\n{\n\tint x;\n}\n");

	const std::optional<Graphite::BracketPair> pair = fixture.At(9);

	REQUIRE(pair.has_value());
	CHECK(pair->Matched);
	CHECK(pair->Open == 9);
	CHECK(pair->Close == 19);
}

TEST_CASE("The search gives up after the scan limit rather than walking the document")
{
	std::string text = "{\n";
	for (std::size_t line = 0; line < Graphite::kBracketScanLines + 10; ++line)
	{
		text += "\tint x;\n";
	}
	text += "}\n";

	Fixture fixture(text);

	const std::optional<Graphite::BracketPair> pair = fixture.At(0);

	REQUIRE(pair.has_value());
	CHECK_FALSE(pair->Matched);
}

TEST_CASE("A caret on ordinary text finds nothing")
{
	Fixture fixture("int x = 1;");

	CHECK_FALSE(fixture.At(4).has_value());
}
