#pragma once

#include <cstdint>

namespace Graphite {

	// Which built-in a scheme inherits from. Typed rather than named, because a base can only
	// ever be one of these — unlike the active scheme's name, which may also be a user's file
	// and therefore stays a string.
	enum class BuiltInScheme : std::uint8_t
	{
		Dark,
		Light
	};
}
