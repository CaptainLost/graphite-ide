#include "corepch.h"

#include "Core/Ui/PanelManager.h"

#include "Core/Base/Assert.h"

#include <imgui.h>

namespace core
{
    Panel* PanelManager::Add(std::unique_ptr<Panel> panel)
    {
        CORE_ASSERT(panel != nullptr, "PanelManager::Add received a null pointer");

        if (Find(panel->Id()) != nullptr)
            return nullptr;

        Panel* reference = panel.get();
        m_panels.push_back(std::move(panel));
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
                continue;

            bool open = true;
            const std::string label = panel->Title() + "###" + panel->Id();

            if (ImGui::Begin(label.c_str(), &open))
                panel->OnUIRender();

            ImGui::End();

            if (!open)
                panel->SetVisible(false);
        }
    }

    void PanelManager::RenderViewMenuItems()
    {
        for (auto& panel : m_panels)
        {
            bool visible = panel->IsVisible();
            if (ImGui::MenuItem(panel->Title().c_str(), nullptr, &visible))
                panel->SetVisible(visible);
        }
    }

    void PanelManager::ForEach(const std::function<void(Panel&)>& action)
    {
        for (auto& panel : m_panels)
            action(*panel);
    }
}
