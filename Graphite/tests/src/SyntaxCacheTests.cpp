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

	// SyntaxCache's output is a deterministic function of the buffer and the width, so a wasted
	// Reset is invisible in the tokens it returns — Reset() followed by a recompute yields
	// byte-identical output to a cache hit. The only way to see the difference is to count how
	// often the cache actually asks for a line to be lexed.
	class CountingLexer final : public Graphite::Lexer
	{
	public:
		Graphite::LineState LexLine(std::string_view line, const Graphite::LineState& entry,
									std::vector<Graphite::Token>& out) const override
		{
			++m_Calls;

			if (!line.empty())
			{
				out.push_back(Graphite::Token{Graphite::TokenKind::Identifier, 0, line.size()});
			}

			return entry;
		}

		std::size_t GetCallCount() const { return m_Calls; }

	private:
		mutable std::size_t m_Calls = 0;
	};

	std::string KindsOf(Graphite::SyntaxCache& cache, const Core::TextBuffer& buffer,
						std::size_t line)
	{
		std::string kinds;

		for (const Graphite::Token& token : cache.TokensOf(buffer, line))
		{
			kinds += token.Kind == Graphite::TokenKind::Comment	  ? 'm'
					 : token.Kind == Graphite::TokenKind::Keyword ? 'k'
					 : token.Kind == Graphite::TokenKind::String  ? 's'
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

// Token offsets are offsets into the TAB-EXPANDED line, so a width change moves every one of
// them. Nothing about this fails loudly if it is missed — the colours land in the wrong columns.
TEST_CASE("SyntaxCache re-lexes when the tab width changes")
{
	const Core::TextBuffer buffer{"\tint x;"};
	Graphite::SyntaxCache cache = CppCache();

	cache.SetTabWidth(4);
	const Graphite::Token firstAtFour = cache.TokensOf(buffer, 0).front();

	cache.SetTabWidth(8);
	const Graphite::Token firstAtEight = cache.TokensOf(buffer, 0).front();

	// "int" starts after the expanded tab, so it starts four bytes later at width eight.
	CHECK(firstAtFour.From == 4);
	CHECK(firstAtEight.From == 8);
	CHECK(firstAtEight.To - firstAtEight.From == firstAtFour.To - firstAtFour.From);
}

// Setting the width it already has must not throw the cache away — that would re-lex the file
// on any frame something asked twice. The tokens returned are a deterministic function of the
// buffer and the width, so a wasted Reset would be invisible in them; only counting the lexer
// calls can catch it.
TEST_CASE("SyntaxCache keeps its states when the tab width is unchanged")
{
	const Core::TextBuffer buffer{"/* open\nstill"};
	CountingLexer lexer;
	Graphite::SyntaxCache cache;
	cache.SetLexer(&lexer);

	cache.SetTabWidth(4);
	cache.TokensOf(buffer, 1);
	const std::size_t callsAfterFirst = lexer.GetCallCount();

	cache.SetTabWidth(4);
	cache.TokensOf(buffer, 1);
	CHECK(lexer.GetCallCount() == callsAfterFirst);

	// A width that actually differs must still cause a re-lex — otherwise this test would pass
	// just because the guard had been deleted outright.
	cache.SetTabWidth(8);
	cache.TokensOf(buffer, 1);
	CHECK(lexer.GetCallCount() > callsAfterFirst);
}
