#pragma once

#include <cstdint>

namespace Graphite {

	enum class ChromeColor : std::uint8_t
	{
		Background,
		Surface,
		SurfaceHovered,
		SurfaceActive,
		Accent,
		Text,
		TextDisabled,

		Border,
		Count
	};
}
