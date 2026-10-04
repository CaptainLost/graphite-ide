#pragma once

#include <imgui.h>

namespace Graphite {

	// Whether the activation a row just reported opens its file for good rather than selecting
	// and previewing it: a double click, or Enter on the row keyboard navigation reached. Asked
	// on the frame the row's Selectable returned true. Every list that opens files - the file
	// tree, the solution tree, Problems, Find in Files, References - asks this, so a gesture
	// means the same wherever it is made.
	inline bool OpensPermanently()
	{
		return ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) || ImGui::IsKeyPressed(ImGuiKey_Enter) ||
			   ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
	}
}
