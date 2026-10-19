#pragma once

#include <imgui.h>

#include <string_view>

namespace Graphite {

	const char* ChevronGlyph(bool open);

	void DrawChevron(bool open);

	float ChevronWidth();

	float TreeRowChevronBoxLeft();

	float TreeRowChevronBoxWidth();

	float TreeRowLabelOffset();

	void DrawTreeMessage(std::string_view message);

	bool ChevronTreeNode(const char* id, ImGuiTreeNodeFlags flags, const char* label = "", const char* note = "");
}
