#pragma once

#include "Search/SearchQuery.h"
#include "UI/HighlightedRow.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace Graphite {

	// WARNING: The views point into the hit's own Text, so the hit must outlive them.
	HighlightedLabel SplitForDisplay(const SearchHit& hit);

	// WARNING: Deleted because a temporary would return views into a string destroyed before
	// they are read. The result is garbage on screen rather than a crash.
	HighlightedLabel SplitForDisplay(SearchHit&&) = delete;

	struct DisplayPath
	{
		std::string Directory;
		std::string Name;
	};

	DisplayPath SplitPathForDisplay(const std::filesystem::path& file,
									const std::filesystem::path& root);
}
