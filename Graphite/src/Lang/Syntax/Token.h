#pragma once

#include "Lang/Api/TokenKind.h"

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
	// a default-constructed one, and SyntaxCache uses equality on this to decide when a re-lex
	// can stop early.
	struct LineState
	{
		enum class Continuation : std::uint8_t
		{
			None,
			BlockComment,
			// LineComment, String and Character are reached only through a backslash at the end
			// of the line. RawString is not — a raw string runs to its delimiter and crosses
			// line boundaries on its own.
			LineComment,
			String,
			Character,
			RawString
		};

		Continuation Kind = Continuation::None;

		// Only meaningful for RawString: R"delim(...)delim" ends on this exact delimiter, and
		// the standard allows up to 16 characters in it.
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
