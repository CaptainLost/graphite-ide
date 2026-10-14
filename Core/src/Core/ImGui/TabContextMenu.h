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

	void DrawTabContextMenu(TabCloseRequests& requests, const std::function<void()>& extraItems = {});
}
