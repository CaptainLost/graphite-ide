#pragma once

#include <cstdint>

namespace Core {

	struct Color
	{
		float r = 0.0f;
		float g = 0.0f;
		float b = 0.0f;
		float a = 1.0f;
	};

	std::uint32_t PackBgr(const Color& color);
}