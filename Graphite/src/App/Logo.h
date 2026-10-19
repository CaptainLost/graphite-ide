#pragma once

#include <imgui.h>

#include <optional>

namespace Graphite {

	// Two colours to draw the logo in instead of its own: its darkest shade becomes ForDarkest, its lightest
	// ForLightest, and every shade between falls between them by its luminance, so the faces keep their order.
	struct LogoTones
	{
		ImU32 ForDarkest = 0;
		ImU32 ForLightest = 0;
	};

	void DrawLogo(ImDrawList* draw, ImVec2 centre, float height, const std::optional<LogoTones>& tones = std::nullopt);

	float LogoWidth(float height);
}
