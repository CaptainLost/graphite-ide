#pragma once

#include "Core/Base/Color.h"

#include <imgui.h>

#include <algorithm>

namespace Core {

	constexpr ImVec4 ToImVec4(const Color& color)
	{
		return ImVec4{color.R, color.G, color.B, color.A};
	}

	// Rounds as ImGui does, adding 0.5 in float, so a byte can differ by one from ChannelToByte's.
	inline ImU32 WithAlphaScaled(ImU32 color, float factor)
	{
		ImVec4 channels = ImGui::ColorConvertU32ToFloat4(color);
		channels.w *= factor;

		return ImGui::ColorConvertFloat4ToU32(channels);
	}

	constexpr ImVec4 WithAlphaScaled(const ImVec4& color, float factor)
	{
		return ImVec4(color.x, color.y, color.z, color.w * factor);
	}

	// The alpha is a byte, 0 to 255: a 0..1 float here trips C4244.
	constexpr ImU32 WithAlphaReplaced(ImU32 color, ImU32 alphaByte)
	{
		return (color & ~IM_COL32_A_MASK) | ((alphaByte << IM_COL32_A_SHIFT) & IM_COL32_A_MASK);
	}

	constexpr ImVec4 WithAlphaReplaced(const ImVec4& color, float alpha)
	{
		return ImVec4(color.x, color.y, color.z, alpha);
	}

	constexpr ImU32 WithOpaqueAlpha(ImU32 color)
	{
		constexpr ImU32 kOpaqueAlphaByte = 0xFFu;

		return WithAlphaReplaced(color, kOpaqueAlphaByte);
	}

	constexpr ImU32 AlphaByteOf(ImU32 color)
	{
		return (color & IM_COL32_A_MASK) >> IM_COL32_A_SHIFT;
	}

	// std::min keeps a NaN channel, since it returns its first argument.
	constexpr ImVec4 WithBrightnessScaled(const ImVec4& color, float factor)
	{
		return ImVec4(std::min(color.x * factor, 1.0f), std::min(color.y * factor, 1.0f),
					  std::min(color.z * factor, 1.0f), color.w);
	}
}
