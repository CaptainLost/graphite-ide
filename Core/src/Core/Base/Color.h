#pragma once

#include <cstdint>

namespace Core {

	struct Color
	{
		float R = 0.0f;
		float G = 0.0f;
		float B = 0.0f;
		float A = 1.0f;
	};

	std::uint32_t PackBgr(const Color& color);
}