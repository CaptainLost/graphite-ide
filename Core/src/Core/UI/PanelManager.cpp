#include "Core/Base/Base.h"

#include "corepch.h"

#include "Core/UI/PanelManager.h"

#include "Core/Base/Assert.h"

#include "Core/Command/CommandRegistry.h"

#include <imgui.h>

namespace Core {

	namespace {
		void RegisterPanelCommand(CommandRegistry& commands, Panel& panel)
		{
			commands.Add({.Id = "panel." + panel.GetId(),
						  .Label = panel.GetTitle(),
						  .Group = "panels",
						  .Run = [&panel] { panel.SetVisible(!panel.IsVisible()); },
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
		// the two in either order. The lambdas capture the panel by reference, which is safe
		// because PanelManager owns it for as long as it owns the registry pointer.
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
		// Only the panels that actually drew last frame. A hidden or tab-hidden panel is not
		// on screen for the user to have aimed at, so it gets no say — which is what makes it
		// safe for a panel to consume keys without anything else having to ask whether it is.
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

	void PanelManager::OnUpdate(Timestep ts)
	{
		for (auto& panel : m_Panels)
			panel->OnUpdate(ts);
	}

	void PanelManager::OnImGuiRender()
	{
		for (auto& panel : m_Panels)
		{
			if (!panel->IsVisible())
			{
				panel->SetWasDrawn(false);
				continue;
			}

			bool open = true;
			const std::string label = panel->GetTitle() + "###" + panel->GetId();

			// Begin returning false means collapsed, or another tab of the same dock node is
			// the selected one — the panel is visible and got its turn, but its body did not
			// run, so it did not draw.
			const bool drawn = ImGui::Begin(label.c_str(), &open);
			panel->SetWasDrawn(drawn);

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