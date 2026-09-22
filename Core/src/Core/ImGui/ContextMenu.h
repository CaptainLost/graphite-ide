#pragma once

#include <imgui.h>

namespace Core {

	// Opens a context menu on right release over the item just drawn.
	//
	// Not ImGui::BeginPopupContextItem: that cannot pass window flags, and without NoMove
	// ImGui lets the menu be dragged by its body - a popup has no title bar, so its whole
	// surface counts as draggable background, and ConfigWindowsMoveFromTitleBarOnly skips
	// its cancel path for exactly those windows. Opening by hand allows BeginPopup, which
	// does take window flags.
	//
	// Returns true between the menu's items and its End, like every other ImGui Begin.
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
