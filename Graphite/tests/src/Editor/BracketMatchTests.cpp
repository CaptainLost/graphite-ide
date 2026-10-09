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

		// The buffer takes its text through the constructor — there is no setter — and the
		// lexer comes from the registry, as SyntaxCacheTests.cpp does it.
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

// Standing just past a closing bracket is the common case at the end of a call, and an editor
// that finds nothing there feels broken.
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

// A brace inside a string is text. Matching into code from it would highlight two characters
// that have nothing to do with each other.
TEST_CASE("A bracket inside a string is not a bracket")
{
	Fixture fixture("const char* s = \"(\";");

	// The bytes are: `"` at 16, `(` at 17, `"` at 18. A caret at 17 is directly on the bracket,
	// so the after-caret path is the one that has to refuse it.
	CHECK_FALSE(fixture.At(17).has_value());
}

TEST_CASE("A bracket inside a comment is not a bracket")
{
	Fixture fixture("// (\nint x;");

	CHECK_FALSE(fixture.At(3).has_value());
}

// The partner search has to skip them too, not just refuse to start on one.
TEST_CASE("A bracket in a string does not close one in code")
{
	Fixture fixture("f(\")\");");

	const std::optional<Graphite::BracketPair> pair = fixture.At(1);

	REQUIRE(pair.has_value());
	CHECK(pair->Matched);
	CHECK(pair->Open == 1);
	CHECK(pair->Close == 5);
}

// Token offsets are offsets into the tab-expanded line, which advances by a character's byte
// length; a column count advances by one per character. The two agree on every ASCII line, so
// only a multi-byte character can tell them apart. Here `ą` is two bytes: the closing bracket
// is byte 6 and character 5, and character 5 still falls inside the string token that spans
// bytes 2 to 5 — so a search mapping bytes to columns would call that bracket string text,
// skip it, and report the pair unmatched.
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

// An unmatched brace at the top of a huge file must not walk the document on every keystroke.
// Past the limit the answer is "no partner", which is also what it looks like on screen.
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
