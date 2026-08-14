#pragma once

#include "Search/SearchQuery.h"

#include <string_view>
#include <vector>

namespace Graphite {

	// Case-insensitive, and about the name alone — the caller passes a file name, never a path.
	bool MaskAccepts(std::string_view mask, std::string_view fileName);

	// Appends every match in `text` to `hits`. Appends rather than assigns so one vector can
	// collect a whole file, or a whole search.
	void FindInText(const SearchQuery& query, std::string_view text,
					const std::filesystem::path& file, std::vector<SearchHit>& hits);
}
