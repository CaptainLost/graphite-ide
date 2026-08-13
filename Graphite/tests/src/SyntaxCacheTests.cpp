#include <doctest/doctest.h>

#include "Core/Text/TextBuffer.h"
#include "Lang/Registry/LanguageRegistry.h"
#include "Lang/Syntax/SyntaxCache.h"

#include <string>

namespace {

	Graphite::SyntaxCache CppCache()
	{
		Graphite::SyntaxCache cache;
		cache.SetLexer(Graphite::LexerFor(Graphite::LanguageId::Cpp));
		return cache;
	}

	std::string KindsOf(Graphite::SyntaxCache& cache, const Core::TextBuffer& buffer,
						std::size_t line)
	{
		std::string kinds;

		for (const Graphite::Token& token : cache.TokensOf(buffer, line))
		{
			kinds += token.Kind == Graphite::TokenKind::Comment  ? 'm'
					 : token.Kind == Graphite::TokenKind::Keyword ? 'k'
					 : token.Kind == Graphite::TokenKind::String	 ? 's'
																 : 'o';
		}

		return kinds;
	}
}

TEST_CASE("SyntaxCache without a lexer reports no tokens")
{
	const Core::TextBuffer buffer{"int x;"};
	Graphite::SyntaxCache cache;

	CHECK_FALSE(cache.HasLexer());
	CHECK(cache.TokensOf(buffer, 0).empty());
}

TEST_CASE("SyntaxCache lexes a line on demand")
{
	const Core::TextBuffer buffer{"int x;"};
	Graphite::SyntaxCache cache = CppCache();

	CHECK(KindsOf(cache, buffer, 0) == "koo");
}

// The point of the cache: line 3's tokens depend on the block comment opened on line 0, so
// asking for line 3 first must still see it.
TEST_CASE("SyntaxCache carries state down from lines never asked about")
{
	const Core::TextBuffer buffer{"/* open\na\nb\nc */ int"};
	Graphite::SyntaxCache cache = CppCache();

	CHECK(KindsOf(cache, buffer, 3) == "mk");
	CHECK(KindsOf(cache, buffer, 1) == "m");
}

TEST_CASE("SyntaxCache answers repeatedly without changing its answer")
{
	const Core::TextBuffer buffer{"/* open\na\nb"};
	Graphite::SyntaxCache cache = CppCache();

	CHECK(KindsOf(cache, buffer, 2) == "m");
	CHECK(KindsOf(cache, buffer, 2) == "m");
	CHECK(KindsOf(cache, buffer, 0) == "m");
}

TEST_CASE("SyntaxCache re-lexes the lines below an invalidated one")
{
	Core::TextBuffer buffer{"/* open\ntext"};
	Graphite::SyntaxCache cache = CppCache();

	CHECK(KindsOf(cache, buffer, 1) == "m");

	// Close the comment on line 0, and line 1 stops being a comment.
	buffer.Erase(0, 7);
	buffer.Insert(0, "int x;");
	cache.Invalidate(0);

	CHECK(KindsOf(cache, buffer, 1) == "o");
}

// The widget is responsible for calling Invalidate, and a path that forgets to would otherwise
// paint stale colours forever. The revision guard turns that bug into a slow frame instead.
TEST_CASE("SyntaxCache drops everything when the revision moved without an invalidation")
{
	Core::TextBuffer buffer{"/* open\ntext"};
	Graphite::SyntaxCache cache = CppCache();

	CHECK(KindsOf(cache, buffer, 1) == "m");

	buffer.Erase(0, 7);
	buffer.Insert(0, "int x;");

	CHECK(KindsOf(cache, buffer, 1) == "o");
}

TEST_CASE("SyntaxCache clamps a line past the end")
{
	const Core::TextBuffer buffer{"int x;"};
	Graphite::SyntaxCache cache = CppCache();

	CHECK(cache.TokensOf(buffer, 99).empty());
}
