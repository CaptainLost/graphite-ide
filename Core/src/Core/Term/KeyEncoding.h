#pragma once

#include "Core/Base/KeyCodes.h"

#include <string>

namespace Core {

	// The bytes a terminal sends for a key press. Empty when the key sends nothing, which
	// covers two cases: a modifier held on its own, and a printable character — those reach
	// the terminal through the host's character input, which has already applied the
	// keyboard layout, so encoding them here would send each one twice.
	std::string EncodeKey(KeyCode key, KeyModifier modifiers);
}
