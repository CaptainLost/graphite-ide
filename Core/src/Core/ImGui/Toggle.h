#pragma once

namespace Core {

	inline constexpr float kToggleWidth = 36.0f;
	inline constexpr float kToggleHeight = 20.0f;

	bool Toggle(const char* id, bool* isOn);
}
