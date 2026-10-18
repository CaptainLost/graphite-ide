#pragma once

#include "Core/ImGui/ToolStrip.h"

#include "Terminal/TerminalRules.h"

#include <imgui.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	inline constexpr std::string_view kTerminalsStripId = "terminals";

	struct ToolStripPanel
	{
		std::string Id;
		std::string Title;
		std::string Icon;
		std::string MenuGroup;
		bool Visible = false;
		std::string Shortcut;
	};

	struct ToolStripBadgeColours
	{
		ImU32 Changes = 0;
		ImU32 Error = 0;
		ImU32 Warning = 0;
		ImU32 Running = 0;
	};

	struct ToolStripInputs
	{
		std::vector<ToolStripPanel> Panels;
		TerminalStripLook Terminals;
		bool HasChanges = false;
		std::size_t Errors = 0;
		std::size_t Warnings = 0;
		ToolStripBadgeColours Colours;
	};

	Core::ToolStripContent BuildToolStripContent(const ToolStripInputs& inputs);
}
