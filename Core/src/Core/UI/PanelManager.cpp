#include "corepch.h"

#include "Core/UI/PanelManager.h"

#include "Core/Base/Base.h"

#include "Core/Base/Assert.h"

#include "Core/Command/CommandRegistry.h"

#include "Core/ImGui/DockTab.h"
#include "Core/ImGui/TabContextMenu.h"

#include "Core/UI/PanelToggle.h"

#include <imgui.h>

#include <utility>

namespace Core {

	namespace {
		// Not a toggle: the click meant to find a panel buried behind another tab would close
		// it. Closing is the tab's own button.
		void ShowAndBringForward(Panel& panel)
		{
			panel.SetVisible(true);
			panel.RequestFocus();
		}

		void RegisterPanelCommand(CommandRegistry& commands, Panel& panel)
		{
			commands.Add({.Id = "panel." + panel.GetId(),
						  .Label = panel.GetTitle(),
						  .ListLabel = "Show " + panel.GetTitle() + " Panel",
						  .Icon = panel.GetIcon(),
						  .Group = "panels." + panel.GetMenuGroup(),
						  .Run = [&panel] { ShowAndBringForward(panel); },
						  .Checked = [&panel] { return panel.IsVisible(); }});
		}
	}

	Panel* PanelManager::AddPanel(Scope<Panel> panel)
	{
		CORE_ASSERT(panel != nullptr, "PanelManager::Add received a null pointer");

		const bool idAlreadyTaken = Find(panel->GetId()) != nullptr;
		if (idAlreadyTaken)
		{
			return nullptr;
		}

		Panel* const addedPanel = panel.get();
		m_Panels.push_back(std::move(panel));

		const bool commandsAlreadyRegistered = m_Commands != nullptr;
		if (commandsAlreadyRegistered)
		{
			RegisterPanelCommand(*m_Commands, *addedPanel);
		}

		return addedPanel;
	}

	Panel* PanelManager::Find(std::string_view id) const
	{
		for (const Scope<Panel>& panel : m_Panels)
		{
			if (panel->GetId() == id)
			{
				return panel.get();
			}
		}

		return nullptr;
	}

	void PanelManager::OnEvent(Event& event)
	{
		for (const Scope<Panel>& panel : m_Panels)
		{
			if (event.Handled)
			{
				return;
			}

			if (panel->HasBeenDrawnOnLatestFrame())
			{
				panel->OnEvent(event);
			}
		}
	}

	bool PanelManager::SetVisible(std::string_view id, bool visible)
	{
		Panel* panel = Find(id);
		if (panel == nullptr)
		{
			return false;
		}

		panel->SetVisible(visible);
		return true;
	}

	bool PanelManager::Toggle(std::string_view id)
	{
		Panel* panel = Find(id);
		if (panel == nullptr)
		{
			return false;
		}

		switch (ChoosePanelToggle(panel->IsVisible(), panel->HasBeenDrawnOnLatestFrame()))
		{
		case PanelToggle::ShowAndBringForward:
			ShowAndBringForward(*panel);
			break;
		case PanelToggle::BringForward:
			panel->RequestFocus();
			break;
		case PanelToggle::Hide:
			panel->SetVisible(false);
			break;
		}

		return true;
	}

	void PanelManager::ForEach(const std::function<void(Panel&)>& visit)
	{
		for (const Scope<Panel>& panel : m_Panels)
		{
			visit(*panel);
		}
	}

	void PanelManager::OnUpdate(Timestep timeSinceLastFrame)
	{
		for (const Scope<Panel>& panel : m_Panels)
		{
			panel->OnUpdate(timeSinceLastFrame);
		}
	}

	void PanelManager::OnImGuiRender(TabCloseRequests& closeRequests)
	{
		const bool isFirstRender = m_FirstRender;
		m_FirstRender = false;

		const ImGuiWindowFlags windowFlags = isFirstRender ? ImGuiWindowFlags_NoFocusOnAppearing : ImGuiWindowFlags_None;

		for (const Scope<Panel>& panel : m_Panels)
		{
			if (!panel->IsVisible())
			{
				panel->SetDrawnOnLatestFrame(false);
				continue;
			}

			bool windowOpen = true;
			const std::string windowName = panel->GetWindowName();

			// SetNextWindowFocus has to come before Begin to reach a window that appears this
			// frame; on a docked window it also selects the tab.
			const bool focusRequested = panel->ConsumeFocusRequest();
			if (focusRequested)
			{
				ImGui::SetNextWindowFocus();
				ImGui::SetNextWindowCollapsed(false);
			}

			// WARNING: A window keeps the padding in force at its Begin, and only its own: its dock tab is drawn by
			// the dock node, and a popup or child begun inside it takes the interface's padding again.
			const bool flush = panel->GetSides() == Panel::Sides::Flush;

			if (flush)
			{
				ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, ImGui::GetStyle().WindowPadding.y));
			}

			// Begin returns false for a docked window whose tab is not selected as well as for a
			// collapsed one.
			const bool bodyRuns = BeginDockedWindow(windowName.c_str(), &windowOpen, windowFlags);

			if (flush)
			{
				ImGui::PopStyleVar();
			}

			panel->SetDrawnOnLatestFrame(bodyRuns);
			DrawDockTab(DockTabLookOf(windowName), closeRequests);

			// WARNING: Only its own Close: Close Others or Close All picked from a document's tab closes the documents
			// beside the panel and leaves the panel open.
			const bool tabMenuAskedToClose = closeRequests.TakeAskedByName(windowName);
			if (tabMenuAskedToClose)
			{
				windowOpen = false;
			}

			DrawTabContextMenu(closeRequests);

			if (bodyRuns)
			{
				panel->OnImGuiRender();
			}

			ImGui::End();

			if (!windowOpen)
			{
				panel->SetVisible(false);
			}
		}
	}

	void PanelManager::RegisterCommands(CommandRegistry& commands)
	{
		m_Commands = &commands;

		for (const Scope<Panel>& panel : m_Panels)
		{
			RegisterPanelCommand(commands, *panel);
		}
	}

	void PanelManager::ResetVisibility()
	{
		for (const Scope<Panel>& panel : m_Panels)
		{
			panel->SetVisible(panel->IsDefaultVisible());
		}
	}
}