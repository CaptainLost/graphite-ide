#include "corepch.h"

#include "Core/Base/Color.h"

#include "Core/Base/Ascii.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace Core {

	namespace {
		constexpr float kLargestChannelByte = 255.0f;
		constexpr int kOpaqueAlphaByte = 255;

		constexpr int kValuesPerHexDigit = 16;
		constexpr std::size_t kHexDigitsPerChannel = 2;
		constexpr std::size_t kHexDigitsWithoutAlpha = 6;
		constexpr std::size_t kHexDigitsWithAlpha = 8;

		constexpr std::uint32_t kGreenShift = 8u;
		constexpr std::uint32_t kBlueShift = 16u;

		std::optional<int> HexByte(std::string_view hexDigits, std::size_t highDigitIndex)
		{
			const std::optional<int> highDigit = HexDigitValue(hexDigits[highDigitIndex]);
			const std::optional<int> lowDigit = HexDigitValue(hexDigits[highDigitIndex + 1]);

			if (!highDigit || !lowDigit)
			{
				return std::nullopt;
			}

			return *highDigit * kValuesPerHexDigit + *lowDigit;
		}
	}

	int ChannelToByte(float channel)
	{
		return static_cast<int>(std::lround(std::clamp(channel, 0.0f, 1.0f) * kLargestChannelByte));
	}

	float ChannelFromByte(int byteValue)
	{
		return static_cast<float>(byteValue) / kLargestChannelByte;
	}

	std::uint32_t PackBgr(const Color& color)
	{
		const std::uint32_t red = static_cast<std::uint32_t>(ChannelToByte(color.R));
		const std::uint32_t green = static_cast<std::uint32_t>(ChannelToByte(color.G));
		const std::uint32_t blue = static_cast<std::uint32_t>(ChannelToByte(color.B));

		return red | (green << kGreenShift) | (blue << kBlueShift);
	}

	std::optional<Color> ParseHexColor(std::string_view hexDigits)
	{
		if (hexDigits.size() != kHexDigitsWithoutAlpha && hexDigits.size() != kHexDigitsWithAlpha)
		{
			return std::nullopt;
		}

		Color color;
		float* const channelsInTextOrder[] = {&color.R, &color.G, &color.B, &color.A};

		for (std::size_t channelIndex = 0; channelIndex * kHexDigitsPerChannel < hexDigits.size(); ++channelIndex)
		{
			const std::optional<int> channelByte = HexByte(hexDigits, channelIndex * kHexDigitsPerChannel);

			if (!channelByte)
			{
				return std::nullopt;
			}

			*channelsInTextOrder[channelIndex] = ChannelFromByte(*channelByte);
		}

		return color;
	}

	std::string ToHexColor(const Color& color)
	{
		const int alphaByte = ChannelToByte(color.A);
		const bool isOpaque = alphaByte == kOpaqueAlphaByte;

		if (isOpaque)
		{
			return std::format("{:02X}{:02X}{:02X}", ChannelToByte(color.R), ChannelToByte(color.G), ChannelToByte(color.B));
		}

		return std::format("{:02X}{:02X}{:02X}{:02X}", ChannelToByte(color.R), ChannelToByte(color.G), ChannelToByte(color.B), alphaByte);
	}
}
