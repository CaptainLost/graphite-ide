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
				// A language server's kinds; the lexer never produces one.
				case Graphite::TokenKind::SemanticType:
				case Graphite::TokenKind::SemanticFunction:
				case Graphite::TokenKind::SemanticMacro:
				case Graphite::TokenKind::SemanticMember:
				case Graphite::TokenKind::SemanticParameter:
				case Graphite::TokenKind::SemanticNamespace: mark = '?'; break;
				// Not a colourable kind — see TokenKind::Count's own comment. No token this
				// lexer produces is ever Count; a default: here would hide a genuinely missing
				// kind instead of failing loudly like this one does.
				case Graphite::TokenKind::Count: mark = '?'; break;
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

	// Which construct a line carries over is the lexer's own business — LineState says only
	// whether there is one. So these tests assert that something carried, and prove what it was
	// by the shape of the line that resumes it, which is the behaviour anyone actually depends on.
	bool CarriedOver(const Graphite::LineState& state) { return state != Graphite::LineState{}; }
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

// A line with nothing but whitespace produces no tokens at all — the gaps between tokens are
// whitespace, so whitespace alone is all gap.
TEST_CASE("CppLexer produces no tokens for a whitespace-only line")
{
	CHECK(Shape("    ") == "    ");
	CHECK(Shape("\t \t") == "   ");
}

// UTF-8 lead and continuation bytes are all >= 0x80, which IsIdentifierStart accepts, so a
// non-ASCII identifier lexes as one token rather than as punctuation per byte.
TEST_CASE("CppLexer reads a non-ASCII identifier as one token")
{
	CHECK(Shape("\xC5\xBColw") == "iiiii");
}

TEST_CASE("CppLexer takes a line comment to the end of the line")
{
	CHECK(Shape("int x; // note") == "kkk i. mmmmmmm");
	CHECK(Exit("int x; // note") == Graphite::LineState{});
}

TEST_CASE("CppLexer closes a block comment on the same line")
{
	CHECK(Shape("a /* c */ b") == "i mmmmmmm i");
	CHECK(Exit("a /* c */ b") == Graphite::LineState{});
}

TEST_CASE("CppLexer carries an unclosed block comment to the next line")
{
	CHECK(Shape("a /* open") == "i mmmmmmm");
	CHECK(CarriedOver(Exit("a /* open")));

	// What it carried: the line after it is comment all the way across.
	CHECK(Shape("still inside", Exit("a /* open")) == "mmmmmmmmmmmm");
}

TEST_CASE("CppLexer resumes a block comment from the entry state")
{
	const Graphite::LineState inside = Exit("a /* open");

	CHECK(Shape("still inside", inside) == "mmmmmmmmmmmm");
	CHECK(CarriedOver(Exit("still inside", inside)));

	CHECK(Shape("done */ x", inside) == "mmmmmmm i");
	CHECK(Exit("done */ x", inside) == Graphite::LineState{});
}

// Block comments do not nest in C++: the first */ closes it, and the second is punctuation.
TEST_CASE("CppLexer does not nest block comments")
{
	CHECK(Shape("/* a /* b */ c") == "mmmmmmmmmmmm i");
}

// A line comment ending in a backslash continues onto the next line, which is how a macro
// gets accidentally commented out and worth colouring correctly.
TEST_CASE("CppLexer continues a line comment past a trailing backslash")
{
	CHECK(CarriedOver(Exit("// note \\")));

	const Graphite::LineState inside = Exit("// note \\");
	CHECK(Shape("still comment", inside) == "mmmmmmmmmmmmm");
	CHECK(Exit("still comment", inside) == Graphite::LineState{});
}

TEST_CASE("CppLexer reads a string and a character literal")
{
	CHECK(Shape("s = \"hi\";") == "i . ssss.");
	CHECK(Shape("c = 'x';") == "i . ccc.");
}

TEST_CASE("CppLexer keeps an escaped quote inside the literal")
{
	CHECK(Shape("\"a\\\"b\"") == "ssssss");
	CHECK(Shape("'\\''") == "cccc");
}

// A backslash escaping a backslash does not escape the quote after it.
TEST_CASE("CppLexer ends a literal after an escaped backslash")
{
	CHECK(Shape("\"a\\\\\" b") == "sssss i");
}

TEST_CASE("CppLexer marks an unterminated literal invalid rather than eating the line")
{
	CHECK(Shape("\"open") == "xxxxx");
	CHECK(Exit("\"open") == Graphite::LineState{});
}

TEST_CASE("CppLexer continues a string past a trailing backslash")
{
	CHECK(CarriedOver(Exit("\"open \\")));
}

// The resume side of the case above: three ways the following line can go once entered inside an
// open string literal, none of which the exit-state check alone exercises.
TEST_CASE("CppLexer resumes a string literal from the entry state")
{
	const Graphite::LineState after = Exit("\"open \\");

	CHECK(CarriedOver(after));

	// Closes: the closing quote ends the literal on this line and the state falls back to the
	// default, same as an ordinary string would.
	CHECK(Shape("end\"", after) == "ssss");
	CHECK(Exit("end\"", after) == Graphite::LineState{});

	// Continues: another trailing backslash keeps the literal open into the line after this one
	// too — resuming does not consume the "carried over" property of the state.
	CHECK(Shape("more\\", after) == "sssss");
	CHECK(CarriedOver(Exit("more\\", after)));

	// Runs out: no closing quote and no trailing backslash is a half-typed line, coloured
	// invalid rather than left open indefinitely.
	CHECK(Shape("abc", after) == "xxx");
	CHECK(Exit("abc", after) == Graphite::LineState{});
}

TEST_CASE("CppLexer reads the string prefixes")
{
	CHECK(Shape("u8\"hi\"") == "ssssss");
	CHECK(Shape("L\"hi\"") == "sssss");
	CHECK(Shape("u\"hi\"") == "sssss");
}

TEST_CASE("CppLexer closes a raw string on the same line")
{
	CHECK(Shape("R\"(a\"b)\"") == "ssssssss");
	CHECK(Exit("R\"(a\"b)\"") == Graphite::LineState{});
}

TEST_CASE("CppLexer carries a raw string and its delimiter to the next line")
{
	const Graphite::LineState after = Exit("R\"json(open");

	CHECK(CarriedOver(after));
	CHECK(after.Delimiter == "json");

	CHECK(Shape("still )notjson\" raw", after) == "sssssssssssssssssss");
	CHECK(Shape("done )json\" x", after) == "sssssssssss i");
	CHECK(Exit("done )json\" x", after) == Graphite::LineState{});
}

// There is no raw character literal, so the R is an identifier and the literal is its own token.
TEST_CASE("CppLexer does not treat R as a prefix before a character literal")
{
	CHECK(Shape("R'x'") == "iccc");
}

// The prefixes that ARE valid before a character literal must keep working.
TEST_CASE("CppLexer reads the character literal prefixes")
{
	CHECK(Shape("u8'x'") == "ccccc");
	CHECK(Shape("L'x'") == "cccc");
}

// Sixteen characters is the longest delimiter the standard allows.
TEST_CASE("CppLexer caps a raw string delimiter at sixteen characters")
{
	const Graphite::LineState after = Exit("R\"0123456789abcdef(open");

	CHECK(CarriedOver(after));
	CHECK(after.Delimiter == "0123456789abcdef");

	// Seventeen is not a delimiter, so the literal is invalid rather than left open.
	CHECK(Exit("R\"0123456789abcdefg(open") == Graphite::LineState{});
}

// A prefix only counts when it touches the quote.
TEST_CASE("CppLexer needs the prefix to touch the quote")
{
	CHECK(Shape("R \"hi\"") == "i ssss");
}

TEST_CASE("CppLexer marks the directive introducer and its name")
{
	// The token covers the # and the directive name only; what follows lexes as ordinary code,
	// so "once" is an identifier.
	CHECK(Shape("#pragma once") == "ppppppp iiii");
	CHECK(Shape("  # include <x>") == "  ppppppppp sss");
}

// Everything after the directive name lexes as ordinary C++, so a macro body is coloured.
TEST_CASE("CppLexer lexes a macro body as code")
{
	CHECK(Shape("#define N 42") == "ppppppp i nn");
}

// The angle-bracket form of an include is a header name, not a less-than: colour it as a
// string, which is what it is.
TEST_CASE("CppLexer reads an angled include as a string")
{
	CHECK(Shape("#include <vector>") == "pppppppp ssssssss");
	CHECK(Shape("#include \"Local.h\"") == "pppppppp sssssssss");
}

// An unclosed angle bracket must not swallow the line — a half-typed include is normal.
TEST_CASE("CppLexer leaves an unclosed angled include as punctuation")
{
	CHECK(Shape("#include <vec") == "pppppppp .iii");
}

TEST_CASE("CppLexer continues a directive past a trailing backslash without special state")
{
	CHECK(Exit("#define M(x) \\") == Graphite::LineState{});
}

// The guard is out.empty(), so any earlier token on the line defeats it — including a comment,
// which real C++ permits before a directive. This case is a known limitation this lexer accepts.
// A # that is not the first thing on the line is ordinary punctuation here.
TEST_CASE("CppLexer does not treat a mid-line hash as a directive")
{
	CHECK(Shape("x #y") == "i .i");
	CHECK(Shape("/* c */ #define X") == "mmmmmmm .iiiiii i");
}

// The angle-bracket exception belongs to #include alone.
TEST_CASE("CppLexer scans an angled header name for include only")
{
	CHECK(Shape("#if <x>") == "ppp .i.");
	CHECK(Shape("#includex <x>") == "ppppppppp .i.");
}
