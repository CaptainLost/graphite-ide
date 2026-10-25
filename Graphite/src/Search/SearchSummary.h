#pragma once

#include "Search/SearchService.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace Graphite {

	inline constexpr std::string_view kSearchHint = "Press Enter to search";

	std::string SearchSummaryText(std::size_t hitCount, const SearchProgress& progress, std::string_view searchedNeedle,
								  std::string_view typedNeedle);
}
