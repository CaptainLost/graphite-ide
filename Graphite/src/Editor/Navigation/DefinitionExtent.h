#pragma once

#include "Language/Syntax/Token.h"

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace Graphite {

	struct LineRange
	{
		std::size_t First = 0;
		std::size_t Last = 0; // inclusive
	};

	inline constexpr std::size_t kDefinitionScanLines = 400;
	inline constexpr std::size_t kDefinitionFallbackLines = 20;

	// `lines[0]` is line `firstLine` of the file, and `tokens` runs in step with `lines`.
	LineRange DefinitionExtent(std::span<const std::string_view> lines, std::span<const std::vector<Token>> tokens,
							   std::size_t firstLine);
}
