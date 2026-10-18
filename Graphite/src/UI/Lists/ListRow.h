#pragma once

#include <imgui.h>
#include <imgui_internal.h>

namespace Graphite {

	struct ListRowColours
	{
		ImVec4 Selected;
		ImVec4 Hovered;
		ImVec4 Pressed;
	};

	ListRowColours CurrentListRowColours();

	float ListRowSpacing();

	float ListRowPitch();

	float ListRowLineHeight();

	float ListRowEndInset();

	float ListRowEnd();

	float ListRowEndPosX();

	float ListLeft();
	float ListRight();

	bool ListRowSelectable(const char* id, bool selected, ImGuiSelectableFlags flags = ImGuiSelectableFlags_None);

	ImVec2 ListTableCellPadding();
}
