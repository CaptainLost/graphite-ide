#pragma once

#include "Language/Api/TokenKind.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	// A half-open byte range of the line it was lexed from. Ranges never overlap and are
	// produced in ascending order; the gaps between them are whitespace.
	struct Token
	{
		TokenKind Kind = TokenKind::Text;
		std::size_t From = 0;
		std::size_t To = 0;

		friend bool operator==(const Token&, const Token&) = default;
	};

	// What a line hands the line below it. A lexer that needs no context between lines returns
	// a default-constructed one. Comparable because a value SyntaxCache stores one of per line
	// has to be, not because anything compares two of these to stop a re-lex early today.
	//
	// Deliberately says nothing about what a state means. Cross-line constructs are each
	// language's own — C++ resumes a raw string on a delimiter, C# resumes one on a count of
	// quotes and carries a brace depth through an interpolated string — so an enum here naming
	// one language's constructs would have to be edited by every language added after it, which
	// is the one thing a shared vocabulary exists to prevent. Storing and comparing is all
	// SyntaxCache does with these, and all the contract needs to be.
	struct LineState
	{
		// Zero is "no continuation" in every language, so a default-constructed state is the
		// start of a file whichever lexer reads it. Every other value belongs to the lexer that
		// produced it and means nothing to any other.
		std::uint32_t Mode = 0;

		// A resume condition too wide for Mode. C++ uses it for a raw string's delimiter, which
		// the standard allows 16 characters in; a lexer that needs no such condition leaves it
		// empty.
		std::string Delimiter;

		friend bool operator==(const LineState&, const LineState&) = default;
	};

	class Lexer
	{
	public:
		virtual ~Lexer() = default;

		// Appends this line's tokens to `out` — appends, so a caller may lex a run of lines into
		// one vector — and returns the state the next line starts in.
		virtual LineState LexLine(std::string_view line, const LineState& entry,
								  std::vector<Token>& out) const = 0;
	};
}
