#include "UI/RememberedPlace.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace Graphite {

	bool HasRememberedPlace(const std::string& windowName)
	{
		const ImGuiWindowSettings* settings = ImGui::FindWindowSettingsByID(ImHashStr(windowName.c_str()));

		if (settings == nullptr)
		{
			return false;
		}

		return settings->DockId == 0 || ImGui::DockBuilderGetNode(settings->DockId) != nullptr;
	}
}
