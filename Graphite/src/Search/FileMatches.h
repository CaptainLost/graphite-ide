#pragma once

#include "Search/SearchQuery.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace Core {

	class TextBuffer;
}

namespace Graphite {

	// WARNING: Deliberately a different type from LineMatch. Using one where the other belongs
	// draws a highlight several characters out on any line holding a tab.
	struct DocumentMatch
	{
		std::size_t From = 0;
		std::size_t To = 0;
	};

	std::vector<DocumentMatch> FindInBuffer(const Core::TextBuffer& buffer,
											const SearchQuery& query);

	std::optional<std::size_t> NextMatch(const std::vector<DocumentMatch>& matches,
										 std::size_t current);

	std::optional<std::size_t> PreviousMatch(const std::vector<DocumentMatch>& matches,
											 std::size_t current);

	std::size_t MatchAtOrAfter(const std::vector<DocumentMatch>& matches, std::size_t offset);
}
