#include "Image/PictureBackground.h"

#include <imgui_internal.h>

namespace Graphite {

	namespace {
		constexpr float kCheckerNearShare = 0.06f;
		constexpr float kCheckerFarShare = 0.14f;
	}

	CheckerboardColours ThemeCheckerboardColours()
	{
		const ImVec4 content = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
		const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);

		return {.Near = ImGui::GetColorU32(ImLerp(content, text, kCheckerNearShare)),
				.Far = ImGui::GetColorU32(ImLerp(content, text, kCheckerFarShare))};
	}
}
