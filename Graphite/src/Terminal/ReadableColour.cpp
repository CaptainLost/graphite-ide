#include "Terminal/ReadableColour.h"

#include <algorithm>
#include <cmath>

namespace Graphite {

	namespace {

		float Linear(unsigned int channel)
		{
			const float value = static_cast<float>(channel) / 255.0f;

			return value <= 0.03928f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
		}

		float Luminance(ImU32 colour)
		{
			const unsigned int red = (colour >> IM_COL32_R_SHIFT) & 0xFFu;
			const unsigned int green = (colour >> IM_COL32_G_SHIFT) & 0xFFu;
			const unsigned int blue = (colour >> IM_COL32_B_SHIFT) & 0xFFu;

			return 0.2126f * Linear(red) + 0.7152f * Linear(green) + 0.0722f * Linear(blue);
		}

		unsigned int Channel(ImU32 colour, int shift)
		{
			return (colour >> shift) & 0xFFu;
		}

		ImU32 Blend(ImU32 from, ImU32 to, float amount)
		{
			const auto mix = [amount](unsigned int a, unsigned int b) {
				return static_cast<unsigned int>(std::lround(static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * amount));
			};

			return IM_COL32(mix(Channel(from, IM_COL32_R_SHIFT), Channel(to, IM_COL32_R_SHIFT)),
							mix(Channel(from, IM_COL32_G_SHIFT), Channel(to, IM_COL32_G_SHIFT)),
							mix(Channel(from, IM_COL32_B_SHIFT), Channel(to, IM_COL32_B_SHIFT)), Channel(from, IM_COL32_A_SHIFT));
		}
	}

	float ContrastRatio(ImU32 first, ImU32 second)
	{
		const float lighter = std::max(Luminance(first), Luminance(second));
		const float darker = std::min(Luminance(first), Luminance(second));

		return (lighter + 0.05f) / (darker + 0.05f);
	}

	ImU32 ReadableOn(ImU32 foreground, ImU32 background, ImU32 towards, float minimum)
	{
		if (ContrastRatio(foreground, background) >= minimum)
		{
			return foreground;
		}

		// WARNING: The scheme's text colour may itself fall short of the minimum, and black or white is what it then
		// heads for, whichever is further from the background.
		const ImU32 target = ContrastRatio(towards, background) >= minimum
								 ? towards
								 : (Luminance(background) > 0.18f ? IM_COL32(0, 0, 0, 255) : IM_COL32(255, 255, 255, 255));

		constexpr int kSteps = 20;

		for (int step = 1; step <= kSteps; ++step)
		{
			const ImU32 moved = Blend(foreground, target, static_cast<float>(step) / static_cast<float>(kSteps));

			if (ContrastRatio(moved, background) >= minimum)
			{
				return moved;
			}
		}

		return Blend(foreground, target, 1.0f);
	}
}
