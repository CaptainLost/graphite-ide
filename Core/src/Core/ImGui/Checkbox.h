#pragma once

#include <imgui.h>

namespace Core {

	inline constexpr float kCheckboxSize = 16.0f;

	// A box that is ticked or not, its label after it. A click on the box or the label flips it, and so
	// does Space while it holds keyboard focus. Returns true on the frame it flips. A label that starts
	// with "##" is an id only: nothing is drawn after the box.
	bool Checkbox(const char* label, bool* value);

	// The same, the box outlined in the given colour while it is not ticked: for a box that has
	// something to say about what it marks.
	bool Checkbox(const char* label, bool* value, const ImVec4& outline);

	// How wide Checkbox draws with this label, for a caller that places it against a right edge.
	float CheckboxWidth(const char* label);
}
