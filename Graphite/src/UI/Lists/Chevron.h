#pragma once

#include <imgui.h>

namespace Graphite {

	// The one mark for whatever opens and closes in a list or a tree: a chevron in the dim text colour.
	const char* ChevronGlyph(bool open);

	// As text at the cursor, so whatever follows it with SameLine lines up with it.
	void DrawChevron(bool open);

	// The wider of the open and the closed chevron, for a column both must fit in.
	float ChevronWidth();

	// ImGui::TreeNodeEx with the chevron where ImGui draws its arrow. The label is drawn where and in the colour
	// TreeNodeEx would draw it, and the tree node stays the last item, for IsItemHovered or a context menu after it.
	bool ChevronTreeNode(const char* id, ImGuiTreeNodeFlags flags, const char* label = "");
}
