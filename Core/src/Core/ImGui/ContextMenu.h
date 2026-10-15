#pragma once

#include <imgui.h>

namespace Core {

	// WARNING: not ImGui::BeginPopupContextItem or BeginPopupContextWindow: they cannot pass window flags, and without
	// NoMove ImGui lets the menu be dragged by its body, since a popup has no title bar to tell the body from.
	inline bool BeginRightClickMenu(const char* popupId, bool clickedHere)
	{
		if (clickedHere)
		{
			ImGui::OpenPopup(popupId);
		}

		return ImGui::BeginPopup(popupId, ImGuiWindowFlags_NoMove);
	}

	inline bool BeginRightClickPopup(const char* popupId)
	{
		const bool clickedLastItem = ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
									 ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);

		return BeginRightClickMenu(popupId, clickedLastItem);
	}
}
