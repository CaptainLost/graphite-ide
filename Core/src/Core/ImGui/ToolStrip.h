#pragma once

#include "Core/ImGui/Theme.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <optional>
#include <string>
#include <vector>

namespace Core {

	struct ToolStripItem
	{
		std::string Id;
		std::string Icon;
		std::string Tooltip;
		bool Open = false;
		std::optional<ImU32> Badge;
	};

	struct ToolStripContent
	{
		std::vector<std::vector<ToolStripItem>> Top;
		std::vector<std::vector<ToolStripItem>> Bottom;
	};

	float ToolStripWidth();

	std::vector<ImRect> LayOutToolStrip(const ToolStripContent& content, ImVec2 origin, float height);

	std::optional<std::string> DrawToolStrip(const ToolStripContent& content, const ChromePalette& palette);
}
