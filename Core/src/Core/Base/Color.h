#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Core {

	struct Color
	{
		float R = 0.0f;
		float G = 0.0f;
		float B = 0.0f;
		float A = 1.0f;
	};

	std::uint32_t PackBgr(const Color& color);

	// RRGGBB or RRGGBBAA, case-insensitive, no leading '#'. Nothing for any other length, any
	// non-hex character, or an empty string — a scheme file is hand-edited, so a malformed
	// colour is an ordinary occurrence and not an error worth throwing over.
	std::optional<Color> ParseHexColor(std::string_view text);

	// Six characters when the colour is fully opaque, eight when it is not, so a scheme written
	// by hand stays as short as what a person would have typed.
	std::string ToHexColor(const Color& color);
}