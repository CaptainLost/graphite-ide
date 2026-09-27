#include "corepch.h"

#include "Core/ImGui/TabContextMenu.h"

#include "Core/ImGui/ContextMenu.h"

#include <imgui_internal.h>

#include <algorithm>
#include <optional>
#include <utility>

namespace Core {

	namespace {
		// The tabs of the node `window` is docked in, in the order the bar shows them. A
		// floating window, or a node whose tab bar is hidden, is a node of one tab.
		std::vector<std::string> NodeTabs(const ImGuiWindow& window)
		{
			const ImGuiDockNode* node = window.DockNode;

			if (node == nullptr || node->TabBar == nullptr)
				return {window.Name};

			std::vector<std::string> names;

			for (const ImGuiTabItem& tab : node->TabBar->Tabs)
			{
				if (tab.Window != nullptr)
					names.emplace_back(tab.Window->Name);
			}

			return names;
		}
	}

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

	void DrawTabContextMenu(TabCloseRequests& requests, const std::function<void()>& extraItems)
	{
		// Read before the popup begins: inside it the current window is the popup itself. Read,
		// not GetCurrentWindow: that one marks the window as written to, and ImGui then stops
		// reporting the tab Begin left as the last item as hovered - so the menu below would
		// never open, and an owner asking about its tab afterwards would be told no.
		const ImGuiWindow& window = *ImGui::GetCurrentWindowRead();

		// The popup id lives in the window's own id stack, so every tab has its own menu.
		if (!BeginRightClickPopup("##tabContextMenu"))
			return;

		const std::vector<std::string> tabs = NodeTabs(window);
		const std::string_view clicked = window.Name;

		if (extraItems)
		{
			extraItems();
			ImGui::Separator();
		}

		const bool hasOthers = !TabsToClose(tabs, clicked, TabCloseAction::Others).empty();
		const bool hasRight = !TabsToClose(tabs, clicked, TabCloseAction::ToTheRight).empty();

		std::optional<TabCloseAction> picked;

		if (ImGui::MenuItem("Close"))
			picked = TabCloseAction::This;

		if (ImGui::MenuItem("Close Others", nullptr, false, hasOthers))
			picked = TabCloseAction::Others;

		if (ImGui::MenuItem("Close to the Right", nullptr, false, hasRight))
			picked = TabCloseAction::ToTheRight;

		if (ImGui::MenuItem("Close All"))
			picked = TabCloseAction::All;

		ImGui::EndPopup();

		if (picked)
		{
			for (std::string& name : TabsToClose(tabs, clicked, *picked))
				requests.Request(std::move(name));
		}
	}
}
