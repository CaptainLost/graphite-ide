#pragma once

#include <imgui.h>

namespace Graphite {

	inline bool OpensPermanently()
	{
		return ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) || ImGui::IsKeyPressed(ImGuiKey_Enter) ||
			   ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
	}
}
