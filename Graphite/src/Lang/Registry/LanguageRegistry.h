#pragma once

#include "Lang/Api/LanguageId.h"
#include "Lang/Syntax/Token.h"

#include <filesystem>

namespace Graphite {

	LanguageId LanguageOfPath(const std::filesystem::path& path);

	// The lexer for a language, or null when it has none yet. The returned lexer is stateless
	// and shared; its lifetime is the program's.
	const Lexer* LexerFor(LanguageId language);
}
