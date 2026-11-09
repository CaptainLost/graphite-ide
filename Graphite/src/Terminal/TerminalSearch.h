#pragma once

#include "Core/Terminal/TerminalScreen.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace Graphite {

	// One place the searched text shows in a terminal: a line, numbered as the selection numbers them, and the cells
	// it covers, half-open.
	struct TerminalMatch
	{
		std::int64_t Line = 0;
		std::size_t FromColumn = 0;
		std::size_t ToColumn = 0;

		std::strong_ordering operator<=>(const TerminalMatch&) const = default;
	};

	// Every place `needle` shows in the scrollback and on the screen, oldest first, case ignored for ASCII letters. A
	// match never spans two lines. Nothing for an empty needle.
	std::vector<TerminalMatch> FindAllInTerminal(const Core::TerminalScreen& screen, std::size_t columns,
												 std::string_view needle);

	enum class TerminalSearchDirection
	{
		Forward,
		Backward
	};

	// The match after `current` going forward, or before it going back, wrapping round at either end; the newest
	// match when there is no current one, as the reader looks at the bottom of a terminal first.
	std::optional<std::size_t> NextTerminalMatch(const std::vector<TerminalMatch>& matches,
												 const std::optional<TerminalMatch>& current,
												 TerminalSearchDirection direction);
}
