#pragma once

#include <imgui.h>

namespace Graphite {

	enum class PictureBackground
	{
		Checkerboard,
		Dark,
		Light
	};

	inline constexpr ImU32 kDarkPictureBackground = IM_COL32(13, 14, 16, 255);
	inline constexpr ImU32 kLightPictureBackground = IM_COL32(243, 244, 246, 255);

	struct CheckerboardColours
	{
		ImU32 Near = 0;
		ImU32 Far = 0;
	};

	// Two greys between the theme's content background and its text, so the squares follow the theme.
	CheckerboardColours ThemeCheckerboardColours();
}
