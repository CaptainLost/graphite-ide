#pragma once

#include "Language/Syntax/Token.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace Graphite {

	// Both sides' tokens for one file's texts, one entry per line, indexed the same way the
	// texts themselves are: line number minus one. Held by whoever owns the texts and rebuilt
	// only when those texts change, never lexed inside a draw call — see the header comment on
	// ColourFileText for why lexing there, once a frame, would be the mistake.
	struct FileTokens
	{
		std::vector<std::vector<Token>> Old;
		std::vector<std::vector<Token>> New;
	};

	// Splits `text` into raw lines: split on '\n', a trailing '\r' stripped from each line, and
	// no phantom line after a final newline. What ColourFileText colours each line of, and also what
	// DiffView draws for a line it has no diff hunk to look up — both callers must agree on
	// exactly the same bytes a token's From/To are offsets into, which is what sharing this one
	// split, rather than each keeping its own copy, guarantees. This is for a caller that only needs the raw lines themselves.
	std::vector<std::string_view> SplitTextLines(std::string_view text);

	// Offsets are into the raw line as SplitTextLines splits it, '\r' stripped, because this view
	// draws the raw line — tab-expanded offsets would land every token after a tab in the wrong place.
	std::vector<std::vector<Token>> ColourFileText(std::string_view text, const std::filesystem::path& path);

	// One line's terminator removed: at most one '\n', and at most one '\r' before it. The
	// single rule for this in the application — SplitTextLines strips exactly this from every
	// line it produces, and a Core::DiffLine carries its own terminator in its text — so the
	// bytes a token's From/To index and the bytes a changed row draws cannot disagree.
	std::string_view StripLineTerminator(std::string_view line);

	// One span of a line drawn in one colour. Kind is unset for text no token covers: the
	// whitespace between two tokens, and the whole line when there is nothing to colour it
	// with.
	struct SyntaxRun
	{
		std::size_t From = 0;
		std::size_t To = 0;
		std::optional<TokenKind> Kind;

		friend bool operator==(const SyntaxRun&, const SyntaxRun&) = default;
	};

	// The runs `line` draws as, given the tokens lexed from it — null for a line that has none,
	// and for a caller that has colouring switched off.
	//
	// Always at least one run, even for an empty line and even for a token vector that covers
	// nothing: a row that draws no run at all sizes no item, so the cursor never advances and
	// the next row lands on top of it.
	//
	// Token offsets are clamped to the line rather than trusted. They came from lexing this
	// same line, so they should already fit, but a clamp turns a mismatch into truncated
	// colouring instead of a substring throwing mid-frame.
	std::vector<SyntaxRun> RunsFor(std::string_view line, const std::vector<Token>* tokens);
}
