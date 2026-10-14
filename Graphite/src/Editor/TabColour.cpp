#include "Editor/TabColour.h"

namespace Graphite {

	namespace {

		constexpr float kPreviewAlpha = 0.6f;
		constexpr float kRoundHalfUp = 0.5f;
		constexpr ImU32 kAlphaChannelMask = 0xFFu;
	}

	std::optional<ImU32> TabTextColour(GitDecoration decoration, bool preview, ImU32 statusColour,
									   ImU32 disabledColour)
	{
		if (decoration == GitDecoration::None)
		{
			return preview ? std::optional<ImU32>{disabledColour} : std::nullopt;
		}

		if (!preview)
		{
			return statusColour;
		}

		const ImU32 statusAlpha = (statusColour >> IM_COL32_A_SHIFT) & kAlphaChannelMask;
		const ImU32 dimmedAlpha = static_cast<ImU32>(static_cast<float>(statusAlpha) * kPreviewAlpha + kRoundHalfUp);
		const ImU32 colourWithoutAlpha = statusColour & ~(kAlphaChannelMask << IM_COL32_A_SHIFT);
		return colourWithoutAlpha | (dimmedAlpha << IM_COL32_A_SHIFT);
	}
}
