#include "Editor/TabColour.h"

namespace Graphite {

	namespace {

		constexpr float kPreviewAlpha = 0.6f;
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

		const ImU32 alpha = (statusColour >> IM_COL32_A_SHIFT) & 0xFFu;
		const ImU32 dimmed = static_cast<ImU32>(static_cast<float>(alpha) * kPreviewAlpha + 0.5f);
		return (statusColour & ~(0xFFu << IM_COL32_A_SHIFT)) | (dimmed << IM_COL32_A_SHIFT);
	}
}
