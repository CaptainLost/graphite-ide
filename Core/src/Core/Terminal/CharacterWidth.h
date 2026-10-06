#pragma once

#include <cstdint>

namespace Core {

	inline constexpr std::uint8_t kNarrowCharacterCells = 1;
	inline constexpr std::uint8_t kWideCharacterCells = 2;

	// Without the wide ranges the whole screen shifts right at the first wide character in a log.
	// A table of ranges, not a full Unicode database, is all a terminal needs.
	std::uint8_t CharacterWidth(char32_t code);
}
