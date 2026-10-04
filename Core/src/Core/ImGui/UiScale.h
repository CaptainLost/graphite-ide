#pragma once

#include <imgui.h>

namespace Core {

	// The factor a length written in pixels at 100% is multiplied by in the window being
	// drawn: the user's own interface scale (style.FontScaleMain) times the scale of the
	// monitor that window is on (style.FontScaleDpi, which ImGui keeps current per viewport).
	// Text already follows it; anything sized by a constant rather than by the font or the
	// style has to go through Scaled to keep up.
	//
	// Only meaningful while a window is being drawn. EndFrame leaves no viewport current,
	// which resets FontScaleDpi to 1, so a call between frames - in OnUpdate, in an event
	// handler - quietly loses the monitor's scale.
	float UiScale();

	float Scaled(float length);
	ImVec2 Scaled(ImVec2 size);

	// The style to draw the chrome with at `scale`: ImGui's default sizes with the theme's
	// on top, multiplied by `scale`. Everything that is not a size - the colours and the
	// font scales - is taken from `current`. Built from the defaults rather than by scaling
	// `current`, so applying it again after a change of scale replaces the old factor
	// instead of multiplying by it.
	//
	// `squareViewportWindows` is for when panels can be undocked into system windows of
	// their own, where rounded corners and a translucent background look out of place.
	ImGuiStyle ScaledStyle(const ImGuiStyle& current, float scale, bool squareViewportWindows);

	// What the sizes in pixels that ImGui keeps - a dock node's reference size - are multiplied by
	// when the scale goes from `previous` to `next`. One when there was no previous scale (the
	// first frame, whose sizes come from the saved layout at the scale it was saved at) or either
	// is not a scale.
	float ScaleChangeFactor(float previous, float next);

	// Multiplies the reference size of every dock node by `factor`. The side panels of a layout
	// are held at those sizes in pixels, while the central node takes what is left, so without
	// this a panel keeps its width when the text in it grows. Between frames only: dock nodes
	// are being laid out while a frame runs.
	void ScaleDockNodeSizes(float factor);
}
