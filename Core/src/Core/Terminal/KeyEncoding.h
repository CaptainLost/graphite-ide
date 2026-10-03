#pragma once

#include "Core/Base/KeyCodes.h"

#include <cstdint>
#include <string>

namespace Core {

	// The bytes a terminal sends for a key press. Empty when the key sends nothing, which
	// covers two cases: a modifier held on its own, and a printable character — those reach
	// the terminal through the host's character input, which has already applied the
	// keyboard layout, so encoding them here would send each one twice.
	std::string EncodeKey(KeyCode key, KeyModifier modifiers);

	// The report of one wheel notch over a cell, for a program that turned mouse reporting on.
	// `column` and `row` count from zero; the report counts from one. `sgr` picks mode 1006's
	// decimal form over the legacy one, which packs each coordinate into a single byte and so
	// cannot name a cell past the 223rd - the last one it can is reported instead.
	std::string EncodeMouseWheel(bool up, std::uint16_t column, std::uint16_t row,
								 KeyModifier modifiers, bool sgr);
}
