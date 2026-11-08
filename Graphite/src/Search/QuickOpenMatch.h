#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	// How well a path, relative to the folder and separated by '/', matches what was typed in Quick Open: every typed
	// character in order, case ignored, spaces ignored. Higher is better; nothing when it does not match at all.
	// Characters in the file's own name, at the start of a word and next to the previous match count for more, so
	// "wsc" finds WorkspaceSettings.cpp before a deep path that only happens to hold the letters.
	std::optional<int> ScoreQuickOpenMatch(std::string_view typed, std::string_view path);

	// The indices of the paths that match, best first, at most `limit`; ties keep the shorter path, then the order
	// given. An empty query keeps the order given.
	std::vector<std::size_t> RankQuickOpenMatches(std::string_view typed, const std::vector<std::string>& paths,
												  std::size_t limit);
}
