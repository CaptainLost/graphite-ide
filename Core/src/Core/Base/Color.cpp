#include "corepch.h"

#include "Core/Base/Color.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace Core {

	namespace {
		std::uint32_t Channel(float value)
		{
			const float scaled = std::clamp(value, 0.0f, 1.0f) * 255.0f;
			return static_cast<std::uint32_t>(std::lround(scaled));
		}

		std::optional<int> HexDigit(char c)
		{
			if (c >= '0' && c <= '9')
			{
				return c - '0';
			}

			if (c >= 'a' && c <= 'f')
			{
				return c - 'a' + 10;
			}

			if (c >= 'A' && c <= 'F')
			{
				return c - 'A' + 10;
			}

			return std::nullopt;
		}

		std::optional<int> HexByte(std::string_view text, std::size_t at)
		{
			const std::optional<int> high = HexDigit(text[at]);
			const std::optional<int> low = HexDigit(text[at + 1]);

			if (!high || !low)
			{
				return std::nullopt;
			}

			return *high * 16 + *low;
		}

		// Rounds to nearest rather than truncating, for the same reason PackBgr does: a channel
		// that came from a byte has to go back to that byte.
		int ToByte(float channel)
		{
			return static_cast<int>(std::lround(std::clamp(channel, 0.0f, 1.0f) * 255.0f));
		}
	}

	std::uint32_t PackBgr(const Color& color)
	{
		return Channel(color.R) | (Channel(color.G) << 8u) | (Channel(color.B) << 16u);
	}

	std::optional<Color> ParseHexColor(std::string_view text)
	{
		if (text.size() != 6 && text.size() != 8)
		{
			return std::nullopt;
		}

		Color color;
		float* const channels[] = {&color.R, &color.G, &color.B, &color.A};

		for (std::size_t index = 0; index * 2 < text.size(); ++index)
		{
			const std::optional<int> byte = HexByte(text, index * 2);

			if (!byte)
			{
				return std::nullopt;
			}

			*channels[index] = static_cast<float>(*byte) / 255.0f;
		}

		return color;
	}

	std::string ToHexColor(const Color& color)
	{
		const int alpha = ToByte(color.A);

		if (alpha == 255)
		{
			return std::format("{:02X}{:02X}{:02X}", ToByte(color.R), ToByte(color.G), ToByte(color.B));
		}

		return std::format("{:02X}{:02X}{:02X}{:02X}", ToByte(color.R), ToByte(color.G), ToByte(color.B), alpha);
	}
}