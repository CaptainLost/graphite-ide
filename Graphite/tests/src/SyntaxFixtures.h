#pragma once

#include "Language/Api/LanguageId.h"
#include "Language/Registry/LanguageRegistry.h"
#include "Language/Syntax/Token.h"

#include <string_view>
#include <vector>

inline std::vector<Graphite::Token> LexCppLine(std::string_view line)
{
	std::vector<Graphite::Token> tokens;
	Graphite::LexerFor(Graphite::LanguageId::Cpp)->LexLine(line, Graphite::LineState{}, tokens);
	return tokens;
}
