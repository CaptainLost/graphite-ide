#pragma once

#include <cstdint>

namespace Core {

	// How many cells a character occupies. Two for the East Asian wide ranges and the emoji
	// block: without this the whole screen shifts right at the first such character in a log.
	// A table of ranges rather than a full Unicode database, which is what a terminal needs
	// and all it needs.
	std::uint8_t CharacterWidth(char32_t code);
}
