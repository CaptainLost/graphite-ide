#include "Core/Base/Base.h"

#include "corepch.h"

#include "Core/UI/PanelManager.h"

#include "Core/Base/Assert.h"

#include "Core/Command/CommandRegistry.h"

#include "Core/ImGui/TabContextMenu.h"

#include <imgui.h>

#include <utility>

namespace Core {

	namespace {
		void RegisterPanelCommand(CommandRegistry& commands, Panel& panel)
		{
			// Opens the panel, or brings it forward when it is already open, rather than toggling:
			// the menu is where a panel is gone looking for, and one that is open but buried
			// behind another tab would otherwise be closed by the click meant to find it. Closing
			// is the tab's own button.
			commands.Add({.Id = "panel." + panel.GetId(),
						  .Label = panel.GetTitle(),
						  .Icon = panel.GetIcon(),
						  .Group = "panels." + panel.GetMenuGroup(),
						  .Run =
							  [&panel]
							  {
								  panel.SetVisible(true);
								  panel.RequestFocus();
							  },
						  .Checked = [&panel] { return panel.IsVisible(); }});
		}
	}

	Panel* PanelManager::Add(Scope<Panel> panel)
	{
		CORE_ASSERT(panel != nullptr, "PanelManager::Add received a null pointer");

		if (Find(panel->GetId()) != nullptr)
			return nullptr;

		Panel* reference = panel.get();
		m_Panels.push_back(std::move(panel));

		// A panel added after RegisterCommands registers as it arrives, so OnAttach can call
		// the two in either order.
		if (m_Commands != nullptr)
		{
			RegisterPanelCommand(*m_Commands, *m_Panels.back());
		}

		return reference;
	}

	Panel* PanelManager::Find(std::string_view id)
	{
		for (auto& panel : m_Panels)
		{
			if (panel->GetId() == id)
				return panel.get();
		}

		return nullptr;
	}

	void PanelManager::OnEvent(Event& event)
	{
		for (auto& panel : m_Panels)
		{
			if (event.Handled)
				return;

			if (panel->WasDrawn())
				panel->OnEvent(event);
		}
	}

	bool PanelManager::SetVisible(std::string_view id, bool visible)
	{
		Panel* panel = Find(id);
		if (panel == nullptr)
			return false;

		panel->SetVisible(visible);
		return true;
	}

	void PanelManager::ForEach(const std::function<void(Panel&)>& visit)
	{
		for (const Scope<Panel>& panel : m_Panels)
		{
			visit(*panel);
		}
	}

	void PanelManager::OnUpdate(Timestep ts)
	{
		for (auto& panel : m_Panels)
			panel->OnUpdate(ts);
	}

	void PanelManager::OnImGuiRender(TabCloseRequests& closeRequests)
	{
		const ImGuiWindowFlags windowFlags =
			std::exchange(m_FirstRender, false) ? ImGuiWindowFlags_NoFocusOnAppearing : ImGuiWindowFlags_None;

		for (auto& panel : m_Panels)
		{
			if (!panel->IsVisible())
			{
				panel->SetWasDrawn(false);
				continue;
			}

			bool open = true;
			const std::string label = panel->GetWindowName();

			// Set before Begin so it reaches a window that is appearing this frame too, and
			// selects the tab when the window shares a dock node with others.
			if (panel->ConsumeFocusRequest())
				ImGui::SetNextWindowFocus();

			// Begin returns false when the window is collapsed or another tab of the same dock
			// node is selected: the panel got its turn, but its body did not run.
			const bool drawn = ImGui::Begin(label.c_str(), &open, windowFlags);
			panel->SetWasDrawn(drawn);

			// A tab's menu asked for this one last frame: the same as its close button.
			if (closeRequests.Take(label))
				open = false;

			DrawTabContextMenu(closeRequests);

			if (drawn)
			{
				panel->OnImGuiRender();
			}

			ImGui::End();

			if (!open)
				panel->SetVisible(false);
		}
	}

	void PanelManager::RegisterCommands(CommandRegistry& commands)
	{
		m_Commands = &commands;

		for (auto& panel : m_Panels)
		{
			RegisterPanelCommand(commands, *panel);
		}
	}

	void PanelManager::ResetVisibility()
	{
		for (auto& panel : m_Panels)
		{
			panel->SetVisible(panel->IsDefaultVisible());
		}
	}
}