#include "corepch.h"

#include "Core/Base/Color.h"

#include <algorithm>
#include <cmath>

namespace Core {

	namespace {
		std::uint32_t Channel(float value)
		{
			const float scaled = std::clamp(value, 0.0f, 1.0f) * 255.0f;
			return static_cast<std::uint32_t>(std::lround(scaled));
		}
	}

	std::uint32_t PackBgr(const Color& color)
	{
		return Channel(color.r) | (Channel(color.g) << 8u) | (Channel(color.b) << 16u);
	}
}