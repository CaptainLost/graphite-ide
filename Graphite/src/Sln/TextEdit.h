#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace Graphite {

	std::string DetectLineEnding(std::string_view text);

	std::string IndentUnit(std::string_view text);

	std::size_t OpeningTagEnd(std::string_view text, std::size_t offset);

	std::size_t ElementEnd(std::string_view text, std::size_t offset,
						   std::string_view name);

	std::pair<std::size_t, std::size_t> LineExtent(std::string_view text,
												   std::size_t position);
}