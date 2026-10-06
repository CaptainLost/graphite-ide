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

	int ChannelToByte(float channel);
	float ChannelFromByte(int byteValue);

	std::uint32_t PackBgr(const Color& color);

	// Empty rather than an error for malformed text: a scheme file is edited by hand, so a
	// malformed colour is an ordinary occurrence and not worth an error.
	std::optional<Color> ParseHexColor(std::string_view hexDigits);

	// An opaque colour leaves out its alpha digits, so a scheme written by hand stays as short as
	// what a person would have typed.
	std::string ToHexColor(const Color& color);
}