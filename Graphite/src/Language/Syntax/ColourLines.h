#pragma once

#include "Language/Api/LanguageId.h"
#include "Language/Syntax/Token.h"

#include <span>
#include <string_view>
#include <vector>

namespace Graphite {

	std::vector<std::vector<Token>> ColourLines(LanguageId language, std::span<const std::string_view> lines);

	std::vector<std::vector<Token>> ColourDeclaration(LanguageId language, std::span<const std::string_view> lines);
}
