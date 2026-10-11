#pragma once

namespace Core {

	inline constexpr float kToggleWidth = 36.0f;
	inline constexpr float kToggleHeight = 20.0f;

	// An on/off switch: a pill whose knob slides right when on, centred in an item as tall as a
	// frame so it lines up with the combos and buttons beside it. Flips on a click and, when
	// focused, on Space. Returns true on the frame it flips.
	bool Toggle(const char* id, bool* value);
}
