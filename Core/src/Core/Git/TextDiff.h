#pragma once

#include "Core/Base/Result.h"

#include <cstddef>
#include <string_view>
#include <vector>

namespace Core {

	// One run of lines that differs between two texts, counted from zero. A side the run has
	// no lines on still has a Start: the boundary the run sits at, which is the number of that
	// side's lines before it. That is what lets a deletion be drawn between two lines.
	struct LineHunk
	{
		std::size_t OldStart = 0;
		std::size_t OldCount = 0;
		std::size_t NewStart = 0;
		std::size_t NewCount = 0;

		friend bool operator==(const LineHunk&, const LineHunk&) = default;
	};

	// Which lines differ between two texts, in ascending order, with no context lines. Needs
	// no repository: both texts are whatever the caller has in hand, a buffer being edited
	// included. Every byte is treated as text; deciding whether a file is text at all is the
	// caller's business.
	Result<std::vector<LineHunk>> DiffTexts(std::string_view oldText, std::string_view newText);
}
