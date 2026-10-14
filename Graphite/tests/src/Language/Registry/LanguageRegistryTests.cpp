#include <doctest/doctest.h>

#include "Language/Registry/LanguageRegistry.h"

#include <span>

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

TEST_CASE("LanguageOfPath ignores the case of the extension because Windows paths are case-insensitive")
{
	CHECK(Graphite::LanguageOfPath("MAIN.CPP") == Graphite::LanguageId::Cpp);
	CHECK(Graphite::LanguageOfPath("Widget.H") == Graphite::LanguageId::Cpp);
}

TEST_CASE("LanguageOfPath treats a dotfile as plain text because its leading dot is the whole name")
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

TEST_CASE("Every language's definition is found under its own id")
{
	for (const Graphite::LanguageId language : Graphite::kAllLanguages)
	{
		CHECK(Graphite::DefinitionOf(language).Id == language);
	}
}

TEST_CASE("C++ names clangd and C# names its servers while the others name none yet")
{
	const Graphite::LanguageDefinition& cpp = Graphite::DefinitionOf(Graphite::LanguageId::Cpp);

	REQUIRE(cpp.Servers.size() == 1);
	CHECK(cpp.Servers[0] == "clangd");

	CHECK(Graphite::LanguageHasServers(Graphite::LanguageId::Cpp));
	CHECK(Graphite::LanguageHasServers(Graphite::LanguageId::CSharp));
	CHECK_FALSE(Graphite::LanguageHasServers(Graphite::LanguageId::TypeScript));
	CHECK_FALSE(Graphite::LanguageHasServers(Graphite::LanguageId::PlainText));
}

TEST_CASE("LanguagesWithServers lists exactly the languages that name a server")
{
	const std::span<const Graphite::LanguageId> languages = Graphite::LanguagesWithServers();

	REQUIRE(languages.size() == 2);
	CHECK(languages[0] == Graphite::LanguageId::Cpp);
	CHECK(languages[1] == Graphite::LanguageId::CSharp);
}

TEST_CASE("graphite.json names each language by its settings name")
{
	CHECK(Graphite::DefinitionOf(Graphite::LanguageId::Cpp).SettingsName == "Cpp");
	CHECK(Graphite::DefinitionOf(Graphite::LanguageId::CSharp).SettingsName == "CSharp");
	CHECK(Graphite::DefinitionOf(Graphite::LanguageId::TypeScript).SettingsName == "TypeScript");
	CHECK(Graphite::DefinitionOf(Graphite::LanguageId::PlainText).SettingsName.empty());
}

TEST_CASE("Only C++ has a lexer")
{
	CHECK(Graphite::LexerFor(Graphite::LanguageId::Cpp) != nullptr);
	CHECK(Graphite::LexerFor(Graphite::LanguageId::CSharp) == nullptr);
	CHECK(Graphite::LexerFor(Graphite::LanguageId::TypeScript) == nullptr);
	CHECK(Graphite::LexerFor(Graphite::LanguageId::PlainText) == nullptr);
}

TEST_CASE("C# is coloured by a grammar and C++ by its lexer")
{
	const Graphite::SyntaxSource csharp = Graphite::SyntaxOf(Graphite::LanguageId::CSharp);
	const Graphite::SyntaxSource cpp = Graphite::SyntaxOf(Graphite::LanguageId::Cpp);

	CHECK(csharp.TreeGrammar != nullptr);
	CHECK(csharp.LineLexer == nullptr);
	CHECK(cpp.LineLexer == Graphite::LexerFor(Graphite::LanguageId::Cpp));
	CHECK(cpp.TreeGrammar == nullptr);
	CHECK(Graphite::SyntaxOf(Graphite::LanguageId::TypeScript) == Graphite::SyntaxSource{});
	CHECK(Graphite::SyntaxOf(Graphite::LanguageId::PlainText) == Graphite::SyntaxSource{});
}

TEST_CASE("LanguageDisplayName is empty for plain text because it is no recognised language")
{
	CHECK(Graphite::LanguageDisplayName(Graphite::LanguageId::PlainText).empty());
}
