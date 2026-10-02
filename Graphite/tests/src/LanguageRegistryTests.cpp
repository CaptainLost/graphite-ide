#include <doctest/doctest.h>

#include "Lang/Registry/LanguageRegistry.h"

TEST_CASE("LanguageOfPath maps the C++ extensions")
{
	CHECK(Graphite::LanguageOfPath("main.cpp") == Graphite::LanguageId::Cpp);
	CHECK(Graphite::LanguageOfPath("TextBuffer.h") == Graphite::LanguageId::Cpp);
	CHECK(Graphite::LanguageOfPath("Widget.hpp") == Graphite::LanguageId::Cpp);
	CHECK(Graphite::LanguageOfPath("legacy.cc") == Graphite::LanguageId::Cpp);
	CHECK(Graphite::LanguageOfPath("legacy.cxx") == Graphite::LanguageId::Cpp);
	CHECK(Graphite::LanguageOfPath("inline.ipp") == Graphite::LanguageId::Cpp);
}

TEST_CASE("LanguageOfPath maps the languages that have no lexer yet")
{
	CHECK(Graphite::LanguageOfPath("Program.cs") == Graphite::LanguageId::CSharp);
	CHECK(Graphite::LanguageOfPath("app.ts") == Graphite::LanguageId::TypeScript);
}

TEST_CASE("LanguageOfPath falls back to plain text")
{
	CHECK(Graphite::LanguageOfPath("README.md") == Graphite::LanguageId::PlainText);
	CHECK(Graphite::LanguageOfPath("Makefile") == Graphite::LanguageId::PlainText);
}

// Windows paths are case-insensitive and .CPP is a file people have.
TEST_CASE("LanguageOfPath ignores the case of the extension")
{
	CHECK(Graphite::LanguageOfPath("MAIN.CPP") == Graphite::LanguageId::Cpp);
	CHECK(Graphite::LanguageOfPath("Widget.H") == Graphite::LanguageId::Cpp);
}

// A leading dot is the whole name, not an extension: .gitignore is not a G file.
TEST_CASE("LanguageOfPath treats a dotfile as plain text")
{
	CHECK(Graphite::LanguageOfPath(".gitignore") == Graphite::LanguageId::PlainText);
	CHECK(Graphite::LanguageOfPath(".clang-format") == Graphite::LanguageId::PlainText);
}

TEST_CASE("LineCommentPrefix is // for every code language and nothing for plain text")
{
	CHECK(Graphite::LineCommentPrefix(Graphite::LanguageId::Cpp) == "//");
	CHECK(Graphite::LineCommentPrefix(Graphite::LanguageId::CSharp) == "//");
	CHECK(Graphite::LineCommentPrefix(Graphite::LanguageId::TypeScript) == "//");
	CHECK_FALSE(Graphite::LineCommentPrefix(Graphite::LanguageId::PlainText).has_value());
}
