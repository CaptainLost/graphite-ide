#pragma once

#include "Search/SearchQuery.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace Graphite {

	// A hit's line cut into the three pieces a results row draws: the text before the match, the
	// match itself, and the text after it. Views into the hit's own Text, so the hit must outlive
	// them — the panel draws straight out of the group that owns them.
	struct HitLine
	{
		std::string_view Before;
		std::string_view Match;
		std::string_view After;
	};

	// Drops the line's leading whitespace and moves the match with it, so a match nested three
	// levels deep starts at the left edge instead of off the right of the panel.
	//
	// Never trims past the match itself: searching for a tab or a run of spaces finds one inside
	// the indentation, and cutting it away would leave the highlight sitting on the wrong text.
	HitLine SplitForDisplay(const SearchHit& hit);

	// Deleted rather than left to the caller's care: passing a temporary returns views into a
	// string destroyed before they are read, and the result is garbage on screen rather than a
	// crash. Writing the tests for this function is exactly how that was found.
	HitLine SplitForDisplay(SearchHit&&) = delete;

	// A file path cut into a dimmed directory and the name, both relative to the workspace root
	// where possible. The root is on every row and carries nothing; without this a results list
	// is a column of "D:\Repos\IDE\" with the interesting part pushed off the edge.
	struct DisplayPath
	{
		// Ends with a separator when non-empty, so the two concatenate without the caller
		// deciding which separator to use. Empty when the file sits directly in the root.
		std::string Directory;

		std::string Name;
	};

	// A file outside the root keeps its absolute path: a relative path climbing out through ".."
	// is longer than what it replaces and harder to read.
	DisplayPath SplitPathForDisplay(const std::filesystem::path& file,
									const std::filesystem::path& root);
}
