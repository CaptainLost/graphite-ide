#pragma once

#include <imgui.h>

namespace Core {

	// WARNING: not ImGui::BeginPopupContextItem: it cannot pass window flags, and without NoMove ImGui lets the menu be dragged by its body, since a popup has no title bar to tell the body from.
	inline bool BeginRightClickPopup(const char* popupId)
	{
		if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
			ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
		{
			ImGui::OpenPopup(popupId);
		}

		return ImGui::BeginPopup(popupId, ImGuiWindowFlags_NoMove);
	}
}
