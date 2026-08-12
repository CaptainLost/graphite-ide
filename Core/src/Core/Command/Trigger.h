#pragma once

#include <cstdint>

namespace Core {

	enum class Trigger : std::uint8_t
	{
		Key,
		WheelUp,
		WheelDown
	};
}