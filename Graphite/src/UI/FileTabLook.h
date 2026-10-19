#pragma once

#include "Core/ImGui/DockTab.h"

#include <imgui.h>

#include <filesystem>
#include <optional>
#include <string>

namespace Graphite {

	struct FileTab
	{
		std::filesystem::path Path;
		std::string Title;
		bool Preview = false;
		std::optional<ImU32> Dot;
	};

	// A file's tab: its kind's badge, then its title, in the italic face while the tab is a preview, then its git dot.
	Core::DockTabLook FileTabLook(const FileTab& tab, ImFont* italicFace);
}
