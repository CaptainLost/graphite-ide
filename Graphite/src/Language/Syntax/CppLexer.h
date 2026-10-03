#pragma once

#include "Language/Syntax/Token.h"

#include <string_view>
#include <vector>

namespace Graphite {

	class CppLexer final : public Lexer
	{
	public:
		LineState LexLine(std::string_view line, const LineState& entry,
						  std::vector<Token>& out) const override;
	};
}
