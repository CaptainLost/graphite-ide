#include "UI/RememberedPlace.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace Graphite {

	namespace {
		constexpr ImGuiID kFloatingDockId = 0;
	}

	bool HasRememberedPlace(const std::string& windowName)
	{
		const ImGuiWindowSettings* settings = ImGui::FindWindowSettingsByID(ImHashStr(windowName.c_str()));

		if (settings == nullptr)
		{
			return false;
		}

		return settings->DockId == kFloatingDockId || ImGui::DockBuilderGetNode(settings->DockId) != nullptr;
	}
}
