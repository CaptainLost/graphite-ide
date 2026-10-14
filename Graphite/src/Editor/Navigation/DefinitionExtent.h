#pragma once

#include "Language/Syntax/Token.h"

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace Graphite {

	struct DefinitionLineRange
	{
		std::size_t First = 0;
		std::size_t Last = 0;
	};

	inline constexpr std::size_t kDefinitionScanLines = 400;
	inline constexpr std::size_t kDefinitionFallbackLines = 20;

	DefinitionLineRange DefinitionExtent(std::span<const std::string_view> linesFromFirstLine,
										 std::span<const std::vector<Token>> tokensPerLine, std::size_t firstLine);
}
