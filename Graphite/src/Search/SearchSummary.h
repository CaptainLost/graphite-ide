#pragma once

#include "Search/SearchService.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	inline constexpr std::string_view kSearchHint = "Press Enter to search";

	// WARNING: Empty until a search with a needle has run, as an empty needle searches nothing; the panel shows
	// kSearchHint then.
	std::optional<std::string> SearchSummaryText(std::size_t hitCount, const SearchProgress& progress,
												 const std::optional<std::string>& searchedNeedle,
												 std::string_view typedNeedle);
}
