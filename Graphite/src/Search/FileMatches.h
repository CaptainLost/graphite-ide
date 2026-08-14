#pragma once

#include "Search/SearchQuery.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace Core {

	class TextBuffer;
}

namespace Graphite {

	// A match's byte range within the whole document, not within a line. Deliberately a
	// different type from LineMatch: the two hold the same two numbers, and using one where the
	// other belongs draws a highlight several characters out on any line holding a tab.
	struct DocumentMatch
	{
		std::size_t From = 0;
		std::size_t To = 0;
	};

	// Every match in the buffer, ascending. Walks the buffer a line at a time rather than
	// materialising it: this runs on every keystroke in the query field, and a 16 MB file would
	// otherwise be copied on each one.
	std::vector<DocumentMatch> FindInBuffer(const Core::TextBuffer& buffer,
											const SearchQuery& query);

	// The index to move to, wrapping at either end. Nothing when there are no matches — a step
	// through nothing must do nothing rather than land on index zero of an empty list.
	std::optional<std::size_t> NextMatch(const std::vector<DocumentMatch>& matches,
										 std::size_t current);

	std::optional<std::size_t> PreviousMatch(const std::vector<DocumentMatch>& matches,
											 std::size_t current);

	// The first match starting at or after `offset`, wrapping to the first when there is none.
	// This is what makes Ctrl+F jump forward from the caret rather than back to the top.
	// Returns zero for an empty list, which the caller ignores along with the list.
	std::size_t MatchAtOrAfter(const std::vector<DocumentMatch>& matches, std::size_t offset);
}
