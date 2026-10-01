#pragma once

#include "Core/Base/Color.h"

struct ImGuiStyle;

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
		// Colours only: the sizes are SetMetrics', so a change of scale can rebuild them
		// without repainting, and a change of palette leaves them alone.
		static void Apply(const ChromePalette& palette);

		// The theme's paddings, roundings and border widths, unscaled. Written into a style
		// that starts from ImGui's defaults - see ScaledStyle - never into the live one, where
		// a scale already applied would be multiplied again.
		static void SetMetrics(ImGuiStyle& style);

		static ChromePalette DefaultPalette();

		static Color GetBorderColor();
	};
}
