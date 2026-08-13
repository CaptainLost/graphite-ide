#include "corepch.h"

#include "Core/Ui/PanelManager.h"

#include "Core/Base/Assert.h"
#include "Core/Command/CommandRegistry.h"

#include <imgui.h>

namespace core
{
    namespace
    {
        void RegisterPanelCommand(CommandRegistry& commands, Panel& panel)
        {
            commands.Add({.id = "panel." + panel.Id(),
                          .label = panel.Title(),
                          .group = "panels",
                          .run = [&panel] { panel.SetVisible(!panel.IsVisible()); },
                          .checked = [&panel] { return panel.IsVisible(); }});
        }
    }

    Panel* PanelManager::Add(std::unique_ptr<Panel> panel)
    {
        CORE_ASSERT(panel != nullptr, "PanelManager::Add received a null pointer");

        if (Find(panel->Id()) != nullptr)
            return nullptr;

        Panel* reference = panel.get();
        m_panels.push_back(std::move(panel));

        // A panel added after RegisterCommands registers as it arrives, so OnAttach can call
        // the two in either order. The lambdas capture the panel by reference, which is safe
        // because PanelManager owns it for as long as it owns the registry pointer.
        if (m_commands != nullptr)
        {
            RegisterPanelCommand(*m_commands, *m_panels.back());
        }

        return reference;
    }

    Panel* PanelManager::Find(std::string_view id)
    {
        for (auto& panel : m_panels)
        {
            if (panel->Id() == id)
                return panel.get();
        }

        return nullptr;
    }

    bool PanelManager::IsCapturingInput() const
    {
        for (const auto& panel : m_panels)
        {
            // A hidden panel draws nothing and so can be reading nothing, whatever it
            // last left behind in its own state.
            if (panel->IsVisible() && panel->IsCapturingInput())
            {
                return true;
            }
        }

        return false;
    }

    bool PanelManager::SetVisible(std::string_view id, bool visible)
    {
        Panel* panel = Find(id);
        if (panel == nullptr)
            return false;

        panel->SetVisible(visible);
        return true;
    }

    void PanelManager::OnUpdate(float deltaSeconds)
    {
        for (auto& panel : m_panels)
            panel->OnUpdate(deltaSeconds);
    }

    void PanelManager::OnUIRender()
    {
        for (auto& panel : m_panels)
        {
            if (!panel->IsVisible())
            {
                panel->OnNotDrawn();
                continue;
            }

            bool open = true;
            const std::string label = panel->Title() + "###" + panel->Id();

            if (ImGui::Begin(label.c_str(), &open))
            {
                panel->OnUIRender();
            }
            else
            {
                // Collapsed, or another tab of the same dock node is the selected one.
                // The panel is still visible and still gets its turn, but its body never
                // ran, so whatever that body was in the middle of has to be ended rather
                // than left waiting invisibly.
                panel->OnNotDrawn();
            }

            ImGui::End();

            if (!open)
                panel->SetVisible(false);
        }
    }

    void PanelManager::RegisterCommands(CommandRegistry& commands)
    {
        m_commands = &commands;

        for (auto& panel : m_panels)
        {
            RegisterPanelCommand(commands, *panel);
        }
    }

    void PanelManager::ResetVisibility()
    {
        for (auto& panel : m_panels)
        {
            panel->SetVisible(panel->DefaultVisible());
        }
    }
}
