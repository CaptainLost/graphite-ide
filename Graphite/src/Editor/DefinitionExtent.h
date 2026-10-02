#pragma once

#include "Lang/Syntax/Token.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string_view>

namespace Graphite {

	struct LineRange
	{
		std::size_t First = 0;
		std::size_t Last = 0; // inclusive
	};

	inline constexpr std::size_t kDefinitionScanLines = 400;
	inline constexpr std::size_t kDefinitionFallbackLines = 20;

	// `lineAt(i)` returns line i's text, or nullopt past the end. Taken as a function so the
	// same walk serves an open TextBuffer and a file read from disk.
	LineRange DefinitionExtent(
		const std::function<std::optional<std::string_view>(std::size_t)>& lineAt,
		std::size_t startLine, const Lexer& lexer);
}
