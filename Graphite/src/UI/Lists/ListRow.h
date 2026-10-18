#pragma once

#include <imgui.h>
#include <imgui_internal.h>

#include <optional>

namespace Graphite {

	struct ListRowColours
	{
		ImVec4 Selected;
		ImVec4 Hovered;
		ImVec4 Pressed;
	};

	ListRowColours CurrentListRowColours();

	std::optional<ImVec4> ListRowHighlight(bool selected, bool hovered, bool held);

	float ListRowSpacing();

	float ListRowPitch();

	float ListRowLineHeight();

	float ListRowTextTop(float rowTop);

	float ListRowEndInset();

	float ListRowEnd();

	float ListRowEndPosX();

	float ListLeft();
	float ListRight();

	bool ListRowSelectable(const char* id, bool selected, ImGuiSelectableFlags flags = ImGuiSelectableFlags_None);

	ImVec2 ListTableCellPadding();

	void PadListTableEnds();

	bool TableRowSelectable(const char* id, bool selected, ImGuiSelectableFlags flags = ImGuiSelectableFlags_None);

	void DrawTableRowHighlight(bool selected, bool hovered);
}
