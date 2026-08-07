#include "IdeLayer.h"

#include "Panels/ConsolePanel.h"
#include "Panels/FileTreePanel.h"
#include "Panels/PlaceholderPanel.h"
#include "Settings.h"

#include "Core/App/Application.h"
#include "Core/Base/Log.h"
#include "Core/Fs/FileSystem.h"

#include <imgui.h>

#include <utility>

namespace ide
{
    IdeLayer::IdeLayer(std::shared_ptr<core::RingBufferSink> sink,
                       std::filesystem::path initialFolder)
        : core::Layer("Ide"), m_sink(std::move(sink)), m_initialFolder(std::move(initialFolder))
    {
        m_layout.left = {"files"};
        m_layout.bottom = {"console"};
        m_layout.center = {"editor"};
    }

    void IdeLayer::OnAttach()
    {
        m_panels.Add(std::make_unique<FileTreePanel>(m_workspace));

        auto* editor = static_cast<PlaceholderPanel*>(
            m_panels.Add(std::make_unique<PlaceholderPanel>("editor", "Editor",
                                                            "The code editor will live here.")));

        m_panels.Add(std::make_unique<ConsolePanel>(m_sink));

        m_events.Subscribe<FileSelected>(
            [editor](const FileSelected& event)
            {
                if (editor != nullptr)
                    editor->SetMessage(core::fs::ToUtf8(event.path));
            });

        CORE_INFO("Registered panels: {}", m_panels.Count());

        std::filesystem::path startFolder = m_initialFolder;
        if (startFolder.empty())
            startFolder = Settings::LoadLastFolder();

        if (!startFolder.empty())
            (void)m_workspace.Open(startFolder);
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

        // Panels render outside the host window — inside it they would dock as ordinary
        // child windows instead of landing in the dockspace nodes.
        m_panels.OnUIRender();
    }

    void IdeLayer::RenderMenuBar()
    {
        if (!ImGui::BeginMenuBar())
            return;

        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Open Folder..."))
                OpenFolderInteractively(m_workspace);

            ImGui::Separator();

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
                CORE_INFO("Layout restored to the default");
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("Log test entries"))
            {
                CORE_TRACE("Diagnostic entry");
                CORE_INFO("Informational entry");
                CORE_WARN("Warning entry");
                CORE_ERROR("Sample error");
            }

            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }
}
