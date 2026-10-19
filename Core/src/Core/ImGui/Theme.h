#pragma once

#include "Core/Base/Color.h"

#include <imgui.h>

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

	// The bright text for what is active or selected: the current text colour pushed six tenths
	// of the way away from the window background, toward white on a dark one and black on a light one.
	ImU32 EmphasisText();

	// The palette's surface, which popups, settings cards and the settings sidebar are filled with.
	ImVec4 SurfaceColour();

	class Theme
	{
	public:
		static void Apply(const ChromePalette& palette);

		static void SetMetrics(ImGuiStyle& style);

		static ChromePalette DefaultPalette();

		static Color GetBorderColor();
	};
}
