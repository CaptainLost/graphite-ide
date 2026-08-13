#pragma once

#include "Lang/Syntax/Token.h"

namespace Graphite {

	class CppLexer final : public Lexer
	{
	public:
		LineState LexLine(std::string_view line, const LineState& entry,
						  std::vector<Token>& out) const override;
	};
}
