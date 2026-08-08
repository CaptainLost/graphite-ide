#include "GraphiteLayer.h"

#include "Panels/ConsolePanel.h"
#include "Panels/ExplorerPanel.h"
#include "Panels/PlaceholderPanel.h"
#include "Settings.h"

#include "Core/App/Application.h"
#include "Core/Base/Log.h"
#include "Core/Fs/FileSystem.h"
#include "Core/Platform/Process.h"

#include <imgui.h>

#include <utility>

namespace graphite
{
    namespace
    {
        /// Shown by the Editor panel until a file is selected, and restored when the
        /// workspace closes. Kept in one place so the two cannot drift apart.
        constexpr const char* kEditorPlaceholder = "The code editor will live here.";
    }

    GraphiteLayer::GraphiteLayer(std::shared_ptr<core::RingBufferSink> sink,
                       std::filesystem::path initialFolder)
        : core::Layer("Ide"), m_sink(std::move(sink)), m_initialFolder(std::move(initialFolder))
    {
        m_layout.left = {"files"};
        m_layout.bottom = {"console"};
        m_layout.center = {"editor"};
    }

    void GraphiteLayer::OnAttach()
    {
        m_panels.Add(std::make_unique<ExplorerPanel>(m_workspace));

        auto* editor = static_cast<PlaceholderPanel*>(
            m_panels.Add(std::make_unique<PlaceholderPanel>("editor", "Editor",
                                                            kEditorPlaceholder)));

        m_panels.Add(std::make_unique<ConsolePanel>(m_sink));

        m_events.Subscribe<FileSelectedEvent>(
            [editor](const FileSelectedEvent& event)
            {
                if (editor != nullptr)
                    editor->SetMessage(core::fs::ToUtf8(event.path));
            });

        m_events.Subscribe<WorkspaceClosedEvent>(
            [editor](const WorkspaceClosedEvent&)
            {
                if (editor != nullptr)
                    editor->SetMessage(kEditorPlaceholder);
            });

        CORE_INFO("Registered panels: {}", m_panels.Count());

        ApplyIgnoredDisplay(IgnoredDisplayFromString(Settings::LoadIgnoredDisplay()), false);

        std::filesystem::path startFolder = m_initialFolder;
        if (startFolder.empty())
            startFolder = Settings::LoadLastFolder();

        if (!startFolder.empty())
            (void)m_workspace.Open(startFolder);
    }

    void GraphiteLayer::OnUpdate(float deltaSeconds)
    {
        m_workspace.Update();
        m_panels.OnUpdate(deltaSeconds);

        // In its own function so its early return cannot swallow the calls above. An
        // earlier revision had this logic inlined here and dropped the panel update
        // silently — nothing failed, because no panel overrides it yet.
        UpdateFocusReload(deltaSeconds);
    }

    void GraphiteLayer::UpdateFocusReload(float deltaSeconds)
    {
        // The process, not the main window: a panel dragged out into its own viewport is
        // a separate OS window, and working in one used to read as having left the
        // application — so coming back to the main window announced a return from an
        // absence that never happened.
        if (!core::platform::IsForeground())
        {
            m_secondsInBackground += deltaSeconds;
            m_appForeground = false;
            return;
        }

        // Only the moment focus returns, not every frame it is held.
        if (!m_appForeground)
        {
            m_appForeground = true;

            // Reload itself does nothing without an open folder, but the log line would
            // still claim a reload happened. A trace that reports work nobody did is
            // worse than no trace at all — it is the sort of thing someone later spends
            // an afternoon believing.
            if (m_secondsInBackground >= kReloadAfterSecondsAway && m_workspace.IsOpen())
            {
                CORE_TRACE("Reloading the workspace after {:.1f}s away", m_secondsInBackground);
                m_workspace.Reload();
            }

            m_secondsInBackground = 0.0f;
        }
    }

    void GraphiteLayer::OnUIRender()
    {
        m_dockSpace.Begin(m_layout);
        RenderMenuBar();
        m_dockSpace.End();

        // Panels render outside the host window — inside it they would dock as ordinary
        // child windows instead of landing in the dockspace nodes.
        m_panels.OnUIRender();
    }

    void GraphiteLayer::ApplyIgnoredDisplay(IgnoredDisplay display, bool persist)
    {
        m_ignoredDisplay = display;

        if (auto* files = static_cast<ExplorerPanel*>(m_panels.Find("files")); files != nullptr)
            files->SetIgnoredDisplay(display);

        if (persist)
            Settings::SaveIgnoredDisplay(IgnoredDisplayToString(display));
    }

    void GraphiteLayer::RenderMenuBar()
    {
        if (!ImGui::BeginMenuBar())
            return;

        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Open Folder..."))
                OpenFolderInteractively(m_workspace);

            if (ImGui::MenuItem("Close Folder", nullptr, false, m_workspace.IsOpen()))
                m_workspace.Close();

            ImGui::Separator();

            if (ImGui::MenuItem("Exit", "Alt+F4"))
                core::Application::Get().Close();

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {
            m_panels.RenderViewMenuItems();
            ImGui::Separator();

            if (ImGui::BeginMenu("Ignored Files"))
            {
                if (ImGui::MenuItem("Show", nullptr, m_ignoredDisplay == IgnoredDisplay::Show))
                    ApplyIgnoredDisplay(IgnoredDisplay::Show, true);

                if (ImGui::MenuItem("Dim", nullptr, m_ignoredDisplay == IgnoredDisplay::Dim))
                    ApplyIgnoredDisplay(IgnoredDisplay::Dim, true);

                if (ImGui::MenuItem("Hide", nullptr, m_ignoredDisplay == IgnoredDisplay::Hide))
                    ApplyIgnoredDisplay(IgnoredDisplay::Hide, true);

                ImGui::EndMenu();
            }

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
