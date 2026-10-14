#pragma once

#include <imgui.h>

namespace Graphite {

	inline constexpr float kTickBoxSize = 16.0f;

	// A box that is ticked or not, its label after it. A click on the box or the label flips it, and so
	// does Space while it holds keyboard focus. Returns true on the frame it flips. A label that starts
	// with "##" is an id only: nothing is drawn after the box.
	bool DrawTickBox(const char* label, bool* value);

	// The same, the box outlined in the given colour while it is not ticked: for a box that has
	// something to say about what it marks.
	bool DrawTickBox(const char* label, bool* value, const ImVec4& outline);

	// How wide DrawTickBox draws with this label, for a caller that places it against a right edge.
	float TickBoxWidth(const char* label);
}
