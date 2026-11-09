#pragma once

#include "Core/ImGui/TabCloseRequests.h"

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

	enum class TabCloseAction
	{
		This,
		Others,
		ToTheRight,
		All
	};

	std::vector<std::string> TabsToClose(std::span<const std::string> tabs, std::string_view clicked,
										 TabCloseAction action);

	// A tab's name as the Go to Tab list shows it: its window name up to the "##" that starts its hidden part.
	std::string_view TabListLabel(std::string_view windowName);

	void DrawTabContextMenu(TabCloseRequests& requests, const std::function<void()>& extraItems = {});
}
