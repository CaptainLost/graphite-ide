#pragma once

#include "Lang/Syntax/Token.h"

#include <filesystem>
#include <string_view>
#include <vector>

namespace Graphite {

	// Lexes a whole text in one top-to-bottom pass, carrying `LineState` from each line into
	// the next so a construct that opens on line 0 still colours line 40 000 correctly. This is
	// deliberately not `SyntaxCache`: that class exists for an editable buffer, where it keeps
	// per-line entry states so one edit can invalidate from a line down rather than re-lex the
	// whole document, and it is keyed to a `Core::TextBuffer`. A diff's blob never changes after
	// it is read, so there is nothing to invalidate and no buffer to key against — one pass over
	// the text is the whole job, which is why this exists alongside `SyntaxCache` rather than
	// routed through it.
	//
	// Lexes the raw text exactly as split, past a trailing '\r' stripped from each line. A
	// token's `From`/`To` are byte offsets into the line it was lexed from, and this view draws
	// the raw line rather than the tab-expanded text the editor feeds its own lexer — feeding
	// expanded text here would land every token after a tab in the wrong place.
	std::vector<std::vector<Token>> LexAllLines(std::string_view text, const Lexer& lexer);

	// Looks up the language and lexer for `path` and runs `LexAllLines` over `text`. Returns an
	// empty vector when the language has no lexer: a file the application cannot lex draws in
	// plain text, which is the correct answer rather than a failure.
	std::vector<std::vector<Token>> LexFileText(std::string_view text,
												 const std::filesystem::path& path);
}
