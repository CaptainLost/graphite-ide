#pragma once

#include <imgui.h>
#include <imgui_internal.h>

namespace Graphite {

	float ListRowPitch();

	float ListRowEndInset();

	float ListRowEnd();

	float ListRowEndPosX();

	float ListLeft();
	float ListRight();

	bool ListRowSelectable(const char* id, bool selected, ImGuiSelectableFlags flags = ImGuiSelectableFlags_None);

	ImVec2 ListTableCellPadding();
}
