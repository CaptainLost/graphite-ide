#pragma once

#include "Language/Api/LanguageId.h"
#include "Language/Syntax/Token.h"

#include <span>
#include <string_view>
#include <vector>

namespace Graphite {

	// Empty when the language has no syntax, which a caller draws as plain text. The lines are
	// coloured as one text, so a comment opened on one of them still colours the next.
	std::vector<std::vector<Token>> ColourLines(LanguageId language, std::span<const std::string_view> lines);

	// For the declaration a server's hover shows: ColourLines alone would read it as a statement in a language whose
	// definition names a member context.
	std::vector<std::vector<Token>> ColourDeclaration(LanguageId language, std::span<const std::string_view> lines);
}
