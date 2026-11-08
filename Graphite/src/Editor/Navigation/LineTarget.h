#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

namespace Graphite {

	// Where Go to Line takes the caret, counting from zero.
	struct LineTarget
	{
		std::size_t Line = 0;
		std::size_t Column = 0;

		friend bool operator==(const LineTarget&, const LineTarget&) = default;
	};

	// "42" or "42:7", as typed, counting from one; a line past the end lands on the last, a column past the line's end
	// on its end, which the caller clamps. Nothing for text that names no line.
	std::optional<LineTarget> ParseLineTarget(std::string_view typed);
}
