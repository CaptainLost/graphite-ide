#include "Git/Branches/RefSectionHeading.h"

#include "UI/Lists/Chevron.h"

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
		DrawChevron(!collapsed);
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
