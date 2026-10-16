#pragma once

#include <imgui.h>

namespace Graphite {

	const char* ChevronGlyph(bool open);

	void DrawChevron(bool open);

	float ChevronWidth();

	bool ChevronTreeNode(const char* id, ImGuiTreeNodeFlags flags, const char* label = "", const char* note = "");
}
