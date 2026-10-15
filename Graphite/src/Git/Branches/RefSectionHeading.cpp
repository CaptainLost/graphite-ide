#include "Git/Branches/RefSectionHeading.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>

namespace Graphite {

	bool DrawRefSectionHeading(const char* id, const std::string& text, bool& collapsed)
	{
		ImGui::PushID(id);

		const ImVec2 rowStart = ImGui::GetCursorPos();
		const bool notSelected = false;

		if (ImGui::Selectable("##section", notSelected, ImGuiSelectableFlags_AllowOverlap))
		{
			collapsed = !collapsed;
		}

		ImGui::SetCursorPos(rowStart);
		ImGui::TextDisabled("%s", collapsed ? ICON_MDI_CHEVRON_RIGHT : ICON_MDI_CHEVRON_DOWN);
		ImGui::SameLine();

		// WARNING: SeparatorText pads its label above and below by default; zeroed, it stays one
		// text row high, which the clipper over these rows counts on.
		ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextPadding, ImVec2(0.0f, 0.0f));
		ImGui::SeparatorText(text.c_str());
		ImGui::PopStyleVar();

		ImGui::PopID();

		return !collapsed;
	}
}