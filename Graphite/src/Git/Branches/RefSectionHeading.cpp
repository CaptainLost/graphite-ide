#include "Git/Branches/RefSectionHeading.h"

#include "UI/Lists/Chevron.h"
#include "UI/Lists/ListRow.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace Graphite {

	bool DrawRefSectionHeading(const char* id, const std::string& title, const std::string& count, bool& collapsed)
	{
		ImGui::PushID(id);

		const ImVec2 rowStart = ImGui::GetCursorPos();
		const bool notSelected = false;

		if (ListRowSelectable("##section", notSelected, ImGuiSelectableFlags_AllowOverlap))
		{
			collapsed = !collapsed;
		}

		ImGui::SetCursorPos(ImVec2(rowStart.x, ListRowTextTop(rowStart.y)));
		DrawChevron(!collapsed);
		ImGui::SameLine();

		// WARNING: SeparatorText pads its label above and below by default; zeroed, it stays one
		// text row high, which the clipper over these rows counts on. Its extra width is the count's,
		// put there with SameLine, and the separator's line starts past it.
		constexpr ImGuiID kNoId = 0;
		const float countRoom = ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize(count.c_str()).x;

		ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextPadding, ImVec2(0.0f, 0.0f));
		ImGui::SeparatorTextEx(kNoId, title.c_str(), nullptr, countRoom);
		ImGui::PopStyleVar();

		ImGui::SameLine();
		ImGui::TextDisabled("%s", count.c_str());

		ImGui::PopID();

		return !collapsed;
	}
}
