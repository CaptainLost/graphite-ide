#include "corepch.h"

#include "Core/ImGui/TabContextMenu.h"

#include "Core/ImGui/ContextMenu.h"

#include <imgui_internal.h>

#include <algorithm>
#include <optional>

namespace Core {

	namespace {
		std::vector<std::string> NodeTabs(const ImGuiWindow& window)
		{
			const ImGuiDockNode* node = window.DockNode;

			if (node == nullptr || node->TabBar == nullptr)
			{
				return {window.Name};
			}

			std::vector<std::string> names;

			for (const ImGuiTabItem& tab : node->TabBar->Tabs)
			{
				if (tab.Window != nullptr)
				{
					names.emplace_back(tab.Window->Name);
				}
			}

			return names;
		}
	}

	std::vector<std::string> TabsToClose(std::span<const std::string> tabs, std::string_view clicked,
										 TabCloseAction action)
	{
		const std::span<const std::string>::iterator clickedTab = std::find(tabs.begin(), tabs.end(), clicked);

		if (clickedTab == tabs.end())
		{
			if (action == TabCloseAction::This)
			{
				return {std::string(clicked)};
			}

			return {};
		}

		switch (action)
		{
		case TabCloseAction::This:
			return {*clickedTab};

		case TabCloseAction::Others:
		{
			std::vector<std::string> others(tabs.begin(), clickedTab);
			others.insert(others.end(), clickedTab + 1, tabs.end());
			return others;
		}

		case TabCloseAction::ToTheRight:
			return std::vector<std::string>(clickedTab + 1, tabs.end());

		case TabCloseAction::All:
			return std::vector<std::string>(tabs.begin(), tabs.end());
		}

		return {};
	}

	void DrawTabContextMenu(TabCloseRequests& requests, const std::function<void()>& extraItems)
	{
		// WARNING: GetCurrentWindow marks the window as written to, and ImGui then stops reporting the tab as hovered, so the menu would never open.
		const ImGuiWindow& window = *ImGui::GetCurrentWindowRead();

		if (!BeginRightClickPopup("##tabContextMenu"))
		{
			return;
		}

		const std::vector<std::string> tabs = NodeTabs(window);
		const std::string_view clicked = window.Name;

		if (extraItems)
		{
			extraItems();
			ImGui::Separator();
		}

		const bool hasOthers = !TabsToClose(tabs, clicked, TabCloseAction::Others).empty();
		const bool hasRight = !TabsToClose(tabs, clicked, TabCloseAction::ToTheRight).empty();

		const char* const noShortcut = nullptr;
		const bool notSelected = false;

		std::optional<TabCloseAction> picked;

		if (ImGui::MenuItem("Close"))
		{
			picked = TabCloseAction::This;
		}

		if (ImGui::MenuItem("Close Others", noShortcut, notSelected, hasOthers))
		{
			picked = TabCloseAction::Others;
		}

		if (ImGui::MenuItem("Close to the Right", noShortcut, notSelected, hasRight))
		{
			picked = TabCloseAction::ToTheRight;
		}

		if (ImGui::MenuItem("Close All"))
		{
			picked = TabCloseAction::All;
		}

		ImGui::EndPopup();

		if (picked)
		{
			for (const std::string& name : TabsToClose(tabs, clicked, *picked))
			{
				requests.Request(name);
			}
		}
	}
}
