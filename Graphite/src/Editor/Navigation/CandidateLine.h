#pragma once

#include "Language/Api/LanguageService.h"
#include "Language/Syntax/Token.h"

#include "Core/Text/DocumentSet.h"

#include <cstddef>
#include <string>
#include <vector>

namespace Graphite {

	struct CandidateLine
	{
		std::string Text;
		std::vector<Token> Tokens;
	};

	CandidateLine LoadCandidateLine(const Core::DocumentSet& documents, const Location& location, std::size_t tabWidth);
}
