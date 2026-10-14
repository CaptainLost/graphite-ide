#pragma once

#include <imgui.h>

namespace Core {

	float UiScale();

	float Scaled(float length);
	ImVec2 Scaled(ImVec2 size);

	ImGuiStyle ScaledStyle(const ImGuiStyle& current, float scale, bool squareViewportWindows);

	float ScaleChangeFactor(float previousScale, float nextScale);

	void ScaleDockNodeSizes(float factor);
}
