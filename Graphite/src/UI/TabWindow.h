#pragma once

#include "Core/ImGui/DockTab.h"

#include <imgui.h>

#include <functional>
#include <optional>
#include <string>

namespace Core {

	class TabCloseRequests;
}

namespace Graphite {

	struct TabWindowState
	{
		bool Fresh = true;
		bool Restored = false;
		bool Focus = false;
	};

	using PlaceNewTab = std::function<void(const std::string& windowName)>;

	struct TabWindowFrame
	{
		bool Visible = false;
		bool Open = true;
		bool Appearing = false;
	};

	// WARNING: The caller ends the window whatever Visible says: Begin returning false still needs End.
	// Without a look, the tab shows what the window's name holds: its glyph and its title.
	TabWindowFrame BeginTabWindow(const std::string& name, TabWindowState& state, const PlaceNewTab& place,
								  Core::TabCloseRequests& closeRequests, ImGuiWindowFlags flags = ImGuiWindowFlags_None,
								  const std::optional<Core::DockTabLook>& look = std::nullopt);

	PlaceNewTab DockInto(ImGuiID node);
}
