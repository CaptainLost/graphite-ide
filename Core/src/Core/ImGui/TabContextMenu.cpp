#include "corepch.h"

#include "Core/ImGui/TabContextMenu.h"

#include <algorithm>

namespace Core {

	std::vector<std::string> TabsToClose(std::span<const std::string> tabs, std::string_view clicked,
										 TabCloseAction action)
	{
		const auto at = std::find(tabs.begin(), tabs.end(), clicked);

		if (at == tabs.end())
		{
			if (action == TabCloseAction::This)
				return {std::string(clicked)};

			return {};
		}

		switch (action)
		{
		case TabCloseAction::This:
			return {*at};

		case TabCloseAction::Others:
		{
			std::vector<std::string> others(tabs.begin(), at);
			others.insert(others.end(), at + 1, tabs.end());
			return others;
		}

		case TabCloseAction::ToTheRight:
			return std::vector<std::string>(at + 1, tabs.end());

		case TabCloseAction::All:
			return std::vector<std::string>(tabs.begin(), tabs.end());
		}

		return {};
	}

	void DrawTabContextMenu(TabCloseRequests&, const std::function<void()>&)
	{
	}
}
