#pragma once

#include "Language/Syntax/Token.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace Graphite {

	// WARNING: Each vector is indexed by line number minus one.
	struct FileTokens
	{
		std::vector<std::vector<Token>> Old;
		std::vector<std::vector<Token>> New;
	};

	std::vector<std::string_view> SplitTextLines(std::string_view text);

	// WARNING: Parses the whole text with no budget, so it must never run on the UI thread.
	std::vector<std::vector<Token>> ColourFileText(std::string_view text, const std::filesystem::path& path);

	std::string_view StripLineTerminator(std::string_view line);

	struct SyntaxRun
	{
		std::size_t From = 0;
		std::size_t To = 0;
		std::optional<TokenKind> Kind;

		friend bool operator==(const SyntaxRun&, const SyntaxRun&) = default;
	};

	std::vector<SyntaxRun> RunsFor(std::string_view line, const std::vector<Token>* tokens);
}
