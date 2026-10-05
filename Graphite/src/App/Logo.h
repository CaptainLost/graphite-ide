#pragma once

#include "Core/Platform/IconResource.h"

#include <imgui.h>

namespace Graphite {

	// The icon resource holding the application's mark without the disc behind it, cropped to
	// the mark, so it sits on whatever surface is under it. Graphite.rc embeds it beside
	// GLFW_ICON, the full icon the taskbar shows; both are exported from Graphite.svg.
	inline constexpr const char* kLogoIcon = "GRAPHITE_MARK";

	// Draws the mark about `height` pixels tall, centred on `centre`: at the nearest size the
	// icon holds an image for, texel for pixel, so it is never scaled — neither by the system
	// on load nor by the GPU on draw. Draws nothing when the resource cannot be read.
	void DrawLogo(ImDrawList* draw, ImVec2 centre, float height);

	// The mark's width as DrawLogo draws it for a given height: the span of columns it paints.
	float LogoWidth(float height);

	// The columns of `image` holding at least one pixel that is not fully transparent, as
	// [First, End). Both zero when every pixel is.
	struct ColumnSpan
	{
		int First = 0;
		int End = 0;
	};

	ColumnSpan OpaqueColumns(const Core::RgbaImage& image);
}
