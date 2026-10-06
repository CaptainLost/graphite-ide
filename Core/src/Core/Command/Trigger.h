#pragma once

#include <cstdint>

namespace Core {

	enum class Trigger : std::uint8_t
	{
		Key,
		WheelUp,
		WheelDown,

		// Modifiers pressed and released with nothing in between. A trigger of its own, because a
		// Key chord of modifiers alone is indistinguishable from no chord at all.
		ModifierTap
	};
}
