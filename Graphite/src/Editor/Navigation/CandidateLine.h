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

	// Read and coloured once when the popup first draws it: coloured every frame, a grammar would re-parse each
	// candidate's file every frame.
	CandidateLine LoadCandidateLine(const Core::DocumentSet& documents, const Location& location, std::size_t tabWidth);
}
