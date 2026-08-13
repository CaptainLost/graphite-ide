#include <doctest/doctest.h>

#include "Lang/Syntax/CppLexer.h"

#include <string>
#include <vector>

namespace {

	// The tests read better as "what colour is each span" than as a list of offsets, so every
	// case renders the lexer's output back over the input: one character per token kind.
	std::string Shape(std::string_view line, Graphite::LineState entry = {})
	{
		const Graphite::CppLexer lexer;
		std::vector<Graphite::Token> tokens;
		lexer.LexLine(line, entry, tokens);

		std::string shape(line.size(), ' ');

		for (const Graphite::Token& token : tokens)
		{
			char mark = '?';

			switch (token.Kind)
			{
				case Graphite::TokenKind::Keyword: mark = 'k'; break;
				case Graphite::TokenKind::Identifier: mark = 'i'; break;
				case Graphite::TokenKind::Number: mark = 'n'; break;
				case Graphite::TokenKind::String: mark = 's'; break;
				case Graphite::TokenKind::Character: mark = 'c'; break;
				case Graphite::TokenKind::Comment: mark = 'm'; break;
				case Graphite::TokenKind::Preprocessor: mark = 'p'; break;
				case Graphite::TokenKind::Punctuation: mark = '.'; break;
				case Graphite::TokenKind::Invalid: mark = 'x'; break;
				case Graphite::TokenKind::Text: mark = ' '; break;
			}

			for (std::size_t at = token.From; at < token.To; ++at)
			{
				shape[at] = mark;
			}
		}

		return shape;
	}

	Graphite::LineState Exit(std::string_view line, Graphite::LineState entry = {})
	{
		const Graphite::CppLexer lexer;
		std::vector<Graphite::Token> tokens;
		return lexer.LexLine(line, entry, tokens);
	}
}

TEST_CASE("CppLexer separates keywords from identifiers")
{
	CHECK(Shape("int value = other;") == "kkk iiiii . iiiii.");
	CHECK(Shape("return x;") == "kkkkkk i.");
}

// "integer" starts with "int" and "Return" is not "return": a keyword is a whole word, and
// the comparison is case-sensitive.
TEST_CASE("CppLexer matches keywords as whole words only")
{
	CHECK(Shape("integer") == "iiiiiii");
	CHECK(Shape("Return") == "iiiiii");
	CHECK(Shape("x_int") == "iiiii");
}

TEST_CASE("CppLexer reads identifiers with underscores and digits")
{
	CHECK(Shape("_m_Value2") == "iiiiiiiii");
}

TEST_CASE("CppLexer reads numbers in every C++ spelling")
{
	CHECK(Shape("0") == "n");
	CHECK(Shape("42u") == "nnn");
	CHECK(Shape("3.14f") == "nnnnn");
	CHECK(Shape("1e-9") == "nnnn");
	CHECK(Shape("0xFF00") == "nnnnnn");
	CHECK(Shape("0b1010") == "nnnnnn");
	CHECK(Shape("1'000'000") == "nnnnnnnnn");
}

// The dot in "3.14" is part of the number; the dot in "a.b" is punctuation. The lexer decides
// by what starts the token, so a number never begins mid-identifier.
TEST_CASE("CppLexer does not start a number inside an identifier")
{
	CHECK(Shape("value2.x") == "iiiiii.i");
}

TEST_CASE("CppLexer marks punctuation and leaves whitespace as text")
{
	CHECK(Shape("a += b;") == "i .. i.");
	CHECK(Shape("{ }") == ". .");
}

TEST_CASE("CppLexer produces no tokens for an empty line and stays in the entry state")
{
	CHECK(Shape("") == "");
	CHECK(Exit("") == Graphite::LineState{});
}

TEST_CASE("CppLexer leaves a plain line in the default state")
{
	CHECK(Exit("int x = 1;") == Graphite::LineState{});
}
