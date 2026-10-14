#pragma once

#include <imgui.h>

namespace Graphite {

	inline constexpr float kTickBoxSize = 16.0f;

	bool DrawTickBox(const char* label, bool* value);

	bool DrawTickBox(const char* label, bool* value, const ImVec4& outline);

	float TickBoxWidth(const char* label);
}
