#pragma once

#include "Core/Base/Color.h"

struct ImGuiStyle;

namespace Core {

	struct ChromePalette
	{
		Color Background;
		Color Surface;
		Color SurfaceHovered;
		Color SurfaceActive;
		Color Accent;
		Color Text;
		Color TextDisabled;
		Color Border;
		Color Content;
	};

	bool IsLightBackground(const Color& background);

	class Theme
	{
	public:
		static void Apply(const ChromePalette& palette);

		static void SetMetrics(ImGuiStyle& style);

		static ChromePalette DefaultPalette();

		static Color GetBorderColor();
	};
}
