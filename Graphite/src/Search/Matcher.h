#pragma once

#include "Search/SearchQuery.h"

#include <cstddef>
#include <filesystem>
#include <string_view>
#include <vector>

namespace Graphite {

	bool MaskAccepts(std::string_view mask, std::string_view fileName);

	struct LineMatch
	{
		std::size_t From = 0;
		std::size_t To = 0;
	};

	// WARNING: `line` must be a single line. It is scanned as-is, so a newline in it would be
	// searched like any other character.
	void FindInLine(const SearchQuery& query, std::string_view line,
					std::vector<LineMatch>& matches);

	void FindInText(const SearchQuery& query, std::string_view text,
					const std::filesystem::path& file, std::vector<SearchHit>& hits);
}
