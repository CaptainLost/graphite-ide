#pragma once

#include "Search/SearchQuery.h"

#include <string_view>
#include <vector>

namespace Graphite {

	// Case-insensitive, and about the name alone — the caller passes a file name, never a path.
	bool MaskAccepts(std::string_view mask, std::string_view fileName);

	// A match's byte range within a single line.
	struct LineMatch
	{
		std::size_t From = 0;
		std::size_t To = 0;
	};

	// Appends every match in one line. `line` must be one line: it is scanned as-is, so a
	// newline in it would be searched like any other character.
	void FindInLine(const SearchQuery& query, std::string_view line,
					std::vector<LineMatch>& matches);

	// Appends every match in `text` to `hits`. Appends rather than assigns so one vector can
	// collect a whole file, or a whole search.
	void FindInText(const SearchQuery& query, std::string_view text,
					const std::filesystem::path& file, std::vector<SearchHit>& hits);
}
