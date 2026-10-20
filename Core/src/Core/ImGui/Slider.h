#pragma once

#include <string_view>

namespace Core {

	inline constexpr float kSliderTrackHeight = 4.0f;
	inline constexpr float kSliderKnobDiameter = 16.0f;

	// A thin track with a round knob and the value, then `unit`, to its right; `width` spans all three.
	bool SliderInt(const char* id, int* value, int smallest, int largest, std::string_view unit, float width);
}
