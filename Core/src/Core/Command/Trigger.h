#pragma once

#include <cstdint>

namespace Core {

	enum class Trigger : std::uint8_t
	{
		Key,
		WheelUp,
		WheelDown,

		// Modifiers pressed and released with nothing in between. Its own kind because a
		// chord of modifiers alone is otherwise indistinguishable from no chord at all, and
		// because it cannot fire on the press: every ordinary shortcut starts with the same
		// keys held down.
		ModifierTap
	};
}