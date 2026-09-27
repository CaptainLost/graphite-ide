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

	// Which of a dock node's tabs an action closes. `tabs` is the node's tabs in the order the
	// tab bar shows them. A `clicked` missing from `tabs` closes only itself, and only
	// through This: without its place in the bar there is no "others" or "right" to speak of.
	std::vector<std::string> TabsToClose(std::span<const std::string> tabs, std::string_view clicked,
										 TabCloseAction action);

	// Opens on right release over the tab of the window just begun, so it is called straight
	// after Begin - before anything else becomes the last item. `extraItems` draws above the
	// close actions, separated from them. A picked close action is recorded in `requests`,
	// never carried out here.
	void DrawTabContextMenu(TabCloseRequests& requests, const std::function<void()>& extraItems = {});
}
