#pragma once

#include "Core/Base/Color.h"

#include <imgui.h>

namespace Core {

	// The measurements a theme sets besides its colours, in unscaled pixels. The defaults are the interface's own.
	struct StyleMetrics
	{
		float PanelGap = 1.0f;
		float PanelRounding = 4.0f;
		float ControlRounding = 6.0f;
		float BorderWidth = 1.0f;
		float ScrollbarWidth = 12.0f;

		bool operator==(const StyleMetrics&) const = default;
	};

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
		StyleMetrics Metrics;
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

		static void SetMetrics(ImGuiStyle& style, const StyleMetrics& metrics = {});

		// The metrics of the palette applied last, which a rebuilt style takes.
		static const StyleMetrics& AppliedMetrics();

		// True once after Apply changes the metrics, until the style has been rebuilt from them.
		static bool TakeMetricsChanged();

		static ChromePalette DefaultPalette();

		static Color GetBorderColor();
	};
}
