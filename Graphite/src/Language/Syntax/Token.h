#pragma once

#include "Language/Api/TokenKind.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	struct Token
	{
		TokenKind Kind = TokenKind::Text;
		std::size_t From = 0;
		std::size_t To = 0;

		friend bool operator==(const Token&, const Token&) = default;
	};

	struct LineState
	{
		std::uint32_t Mode = 0;

		std::string Delimiter;

		friend bool operator==(const LineState&, const LineState&) = default;
	};

	class Lexer
	{
	public:
		virtual ~Lexer() = default;

		virtual LineState LexLine(std::string_view line, const LineState& entry,
								  std::vector<Token>& out) const = 0;
	};
}
