#include "IdeLayer.h"

#include "Panels/ConsolePanel.h"
#include "Panels/PlaceholderPanel.h"

#include "Core/App/Application.h"
#include "Core/Base/Log.h"

#include <imgui.h>

#include <utility>

namespace ide
{
    IdeLayer::IdeLayer(std::shared_ptr<core::RingBufferSink> sink)
        : core::Layer("Ide"), m_sink(std::move(sink))
    {
        m_layout.left = {"files"};
        m_layout.bottom = {"console"};
        m_layout.center = {"editor"};
    }

    void IdeLayer::OnAttach()
    {
        m_panels.Add(std::make_unique<PlaceholderPanel>("files", "Files",
                                                        "The file tree will appear here."));
        m_panels.Add(std::make_unique<PlaceholderPanel>("editor", "Editor",
                                                        "The code editor will appear here."));
        m_panels.Add(std::make_unique<ConsolePanel>(m_sink));

        CORE_INFO("Registered panels: {}", m_panels.Count());
    }

    void IdeLayer::OnUpdate(float deltaSeconds)
    {
        m_panels.OnUpdate(deltaSeconds);
    }

    void IdeLayer::OnUIRender()
    {
        m_dockSpace.Begin(m_layout);
        RenderMenuBar();
        m_dockSpace.End();

        // Panels are rendered outside the host window — otherwise they'd
        // dock inside it as ordinary child windows instead of landing in
        // the dockspace nodes.
        m_panels.OnUIRender();
    }

    void IdeLayer::RenderMenuBar()
    {
        if (!ImGui::BeginMenuBar())
            return;

        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Exit", "Alt+F4"))
                core::Application::Get().Close();

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {
            m_panels.RenderViewMenuItems();
            ImGui::Separator();

            if (ImGui::MenuItem("Reset Layout"))
            {
                m_panels.ForEach([](core::Panel& panel) { panel.SetVisible(true); });
                m_dockSpace.RequestLayoutReset();
                CORE_INFO("Layout reset to default");
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("Log test entries"))
            {
                CORE_TRACE("Diagnostic entry");
                CORE_INFO("Informational entry");
                CORE_WARN("Warning");
                CORE_ERROR("Sample error");
            }

            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }
}
