#pragma once

#include "Lang/Api/LanguageId.h"
#include "Lang/Syntax/Token.h"

#include <filesystem>
#include <string_view>

namespace Graphite {

	LanguageId LanguageOfPath(const std::filesystem::path& path);

	// What to call a language when telling the user something about it. Empty for PlainText,
	// which is not a language so much as the absence of a recognised one — nothing true can be
	// said about "plain text" that a reader wants to hear.
	std::string_view LanguageDisplayName(LanguageId language);

	// The lexer for a language, or null when it has none yet. The returned lexer is stateless
	// and shared; its lifetime is the program's.
	const Lexer* LexerFor(LanguageId language);
}
