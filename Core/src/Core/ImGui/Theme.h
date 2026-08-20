#pragma once

#include "Core/Base/Color.h"

namespace Core {

	// The eight colours the application's chrome is drawn from. Eight rather than one per
	// ImGuiCol_, because eight is what Apply below actually distinguishes — and because a
	// caller that had to name fifty ImGui enumerators would be coupled to ImGui's own
	// vocabulary, which is free to rename them.
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
	};

	class Theme
	{
	public:
		// Takes the palette rather than choosing it. An application composed on Core decides
		// what its chrome looks like; Core knows how to paint one, not which one is right.
		static void Apply(const ChromePalette& palette);

		static ChromePalette DefaultPalette();

		static Color GetBorderColor();
	};
}
