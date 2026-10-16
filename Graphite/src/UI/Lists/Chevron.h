#pragma once

#include <imgui.h>

namespace Graphite {

	// The one mark for whatever opens and closes in a list or a tree: a chevron in the dim text colour.
	const char* ChevronGlyph(bool open);

	// As text at the cursor, so whatever follows it with SameLine lines up with it.
	void DrawChevron(bool open);

	// The wider of the open and the closed chevron, for a column both must fit in.
	float ChevronWidth();

	// A tree node with the chevron where ImGui draws its arrow, used as ImGui::TreeNodeEx is: SetNextItemOpen before
	// it, TreePop after it when it is open. A plain node is a list row, ListRowSelectable across the whole list; a
	// framed one, a collapsing header, is ImGui's own. The label is drawn where and in the colour TreeNodeEx would draw
	// it, and the row's item stays the last one, for IsItemHovered or a context menu after it. A note, a count or a
	// cost, follows the label dim, as every number in a list does.
	bool ChevronTreeNode(const char* id, ImGuiTreeNodeFlags flags, const char* label = "", const char* note = "");
}
