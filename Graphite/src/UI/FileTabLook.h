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

		// The folder name of the other worktree the file belongs to; empty for the open folder's own files.
		std::string Worktree;
	};

	// A file's tab: its kind's badge, then its title, then " · <worktree>" dimmed for a file of another worktree, in the
	// italic face while the tab is a preview, then its git dot.
	Core::DockTabLook FileTabLook(const FileTab& tab, ImFont* italicFace);
}
