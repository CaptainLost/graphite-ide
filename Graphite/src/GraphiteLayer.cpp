#include "GraphiteLayer.h"

#include "Editor/StatusBar.h"
#include "Panels/ConsolePanel.h"
#include "Panels/ExplorerPanel.h"
#include "Settings.h"

#include "Core/App/Application.h"
#include "Core/Base/Log.h"
#include "Core/Fs/FileSystem.h"
#include "Core/Platform/Process.h"
#include "Core/Text/Editing.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <utility>

namespace graphite
{
    namespace
    {
        constexpr const char* kUnsavedWorkTitle = "Unsaved changes##ExitUnsavedWork";
    }

    GraphiteLayer::GraphiteLayer(std::shared_ptr<core::RingBufferSink> sink,
                       std::filesystem::path initialFolder)
        : core::Layer("Ide"), m_sink(std::move(sink)), m_initialFolder(std::move(initialFolder))
    {
        m_layout.left = {"files"};
        m_layout.bottom = {"console"};
        m_layout.center = {};
    }

    void GraphiteLayer::OnAttach()
    {
        m_panels.Add(std::make_unique<ExplorerPanel>(m_workspace));

        m_panels.Add(std::make_unique<ConsolePanel>(m_sink));

        m_events.Subscribe<OpenFileRequested>(
            [this](const OpenFileRequested& event)
            {
                m_documentWindows.RequestOpen(event.path, event.permanent);
            });

        m_events.Subscribe<WorkspaceClosedEvent>([this](const WorkspaceClosedEvent&)
                                                 { m_documents.CloseAll(); });

        m_events.Subscribe<WorkspaceRootMovedEvent>(
            [this](const WorkspaceRootMovedEvent& event)
            { m_documentWindows.HandleRootMoved(event.from, event.to); });

        m_events.Subscribe<WorkspaceFilesChangedEvent>(
            [this](const WorkspaceFilesChangedEvent& event)
            { m_documentWindows.HandleFilesChanged(event.directories, event.everything); });

        CORE_INFO("Registered panels: {}", m_panels.Count());

        ApplyIgnoredDisplay(IgnoredDisplayFromString(Settings::LoadIgnoredDisplay()), false);

        const float storedFontSize = Settings::LoadEditorFontSize();

        if (storedFontSize >= kMinimumEditorFontSize && storedFontSize <= kMaximumEditorFontSize)
            m_editorFontSize = storedFontSize;

        m_indentWithSpaces = Settings::LoadIndentWithSpaces();

        if (const std::size_t storedIndentWidth = Settings::LoadIndentWidth();
            storedIndentWidth != 0)
        {
            m_indentWidth = storedIndentWidth;
        }

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

        // Watchers drop events — buffer overflow, or a tool touching many files at once —
        // and returning from another application is exactly when that matters. Reading
        // the OS-level flag directly (rather than reusing core::platform::IsForeground(),
        // which UpdateFocusReload above already does) is deliberate: that one answers for
        // the whole process, true for a panel dragged into its own viewport just as much
        // as for the main window, where a stale-content sweep only makes sense for the
        // window whose editors are actually on screen. ImGuiIO::AppFocusLost is not this:
        // it is a one-frame flag the backend sets on focus *loss*, so it would fire the
        // sweep as the user leaves rather than as they return.
        auto* handle = static_cast<GLFWwindow*>(core::Application::Get().GetWindow().NativeHandle());
        m_documentWindows.NotifyWindowFocus(glfwGetWindowAttrib(handle, GLFW_FOCUSED) != 0);
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
        DrawStatusBar(m_documents, m_workspace.Root(), m_documentWindows.StatusMessage());

        m_dockSpace.Begin(m_layout);
        RenderMenuBar();
        m_dockSpace.End();

        // Never nested inside the dockspace host's Begin/End above, or any other
        // window's - see RenderCloseConfirm's own comment for why that matters to a
        // modal popup.
        RenderCloseConfirm();

        // Panels render outside the host window — inside it they would dock as ordinary
        // child windows instead of landing in the dockspace nodes.
        m_panels.OnUIRender();

        const core::text::IndentStyle indentStyle{m_indentWithSpaces, m_indentWidth};
        const float zoom =
            m_documentWindows.Render(m_editorFontSize, indentStyle, m_dockSpace.CenterNodeId());

        if (zoom != 0.0f)
            SetEditorFontSize(m_editorFontSize + zoom);

        // ImGui::Shortcut, not a raw IsKeyPressed(..., false) plus a manual KeyCtrl
        // check: Shortcut() matches the modifier set exactly (so Ctrl+Alt+= no longer
        // also zooms) and routes through the same key-ownership machinery InputText
        // uses to claim the keyboard, so these do not fire while the file/folder rename
        // field (FileTreeView, SolutionTreeView) is active. RouteGlobal is needed
        // because this runs after every window this frame has already Begin/End'd, so
        // there is no "current" window whose focus a RouteFocused check could compare
        // against. Each pair (main key, keypad key) is evaluated unconditionally so
        // routing is registered for both even when the other already fired this frame -
        // a short-circuited `||` would skip registering the second on any frame the
        // first was pressed, corrupting its routing for the next frame.
        //
        // The View menu's Zoom In/Out/Reset items show these same combinations as their
        // shortcut text, and both routes end at SetEditorFontSize, so a key press and a
        // menu click always agree on what "zoom in", "zoom out" and "reset" mean.
        const bool resetZoom = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_0, ImGuiInputFlags_RouteGlobal);
        const bool resetZoomKeypad =
            ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Keypad0, ImGuiInputFlags_RouteGlobal);

        if (resetZoom || resetZoomKeypad)
            SetEditorFontSize(kDefaultEditorFontSize);

        const bool zoomIn = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Equal, ImGuiInputFlags_RouteGlobal);
        const bool zoomInKeypad =
            ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_KeypadAdd, ImGuiInputFlags_RouteGlobal);

        if (zoomIn || zoomInKeypad)
            SetEditorFontSize(m_editorFontSize + kEditorFontSizeStep);

        const bool zoomOut = ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Minus, ImGuiInputFlags_RouteGlobal);
        const bool zoomOutKeypad =
            ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_KeypadSubtract, ImGuiInputFlags_RouteGlobal);

        if (zoomOut || zoomOutKeypad)
            SetEditorFontSize(m_editorFontSize - kEditorFontSizeStep);
    }

    void GraphiteLayer::SetEditorFontSize(float newSize)
    {
        const float clamped =
            std::clamp(newSize, kMinimumEditorFontSize, kMaximumEditorFontSize);

        if (clamped == m_editorFontSize)
            return;

        m_editorFontSize = clamped;
        Settings::SaveEditorFontSize(m_editorFontSize);
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

            if (ImGui::MenuItem("Open Solution..."))
                OpenSolutionInteractively(m_workspace);

            ImGui::Separator();

            // "Workspace" rather than "Folder": closing drops the solution and the
            // selection along with the folder, and the menu offers two ways to open one.
            // Naming only the folder would leave the other two looking like they survive.
            if (ImGui::MenuItem("Close Workspace", nullptr, false, m_workspace.IsOpen()))
                RequestCloseConfirmation(ClosePurpose::CloseWorkspace);

            ImGui::Separator();

            // Through the window's close flag, not Application::Close() directly:
            // Application's main loop only asks a layer's OnCloseRequested when
            // Window::ShouldClose() is true, and Close() alone never sets it. Routing
            // through RequestClose() puts Exit through the exact same question as the
            // OS close button and Alt+F4.
            if (ImGui::MenuItem("Exit", "Alt+F4"))
                core::Application::Get().GetWindow().RequestClose();

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {
            m_panels.RenderViewMenuItems();
            ImGui::Separator();

            // Nested under Git so it is obvious whose rules decide what is ignored — the
            // answer comes from .gitignore, .git/info/exclude and the user's global
            // configuration, and nothing in "Ignored Files" alone said so. The submenu
            // is also where a branch indicator and file status belong when they arrive.
            if (ImGui::BeginMenu("Git"))
            {
                // Adjectives, not verbs: the tick beside one of them makes these a state
                // rather than a command, and four levels deep "Dim" alone no longer says
                // what it would dim. The submenu names the subject and these name the
                // treatment, so it reads as "Ignored Files: Dimmed".
                if (ImGui::BeginMenu("Ignored Files"))
                {
                    if (ImGui::MenuItem("Visible", nullptr,
                                        m_ignoredDisplay == IgnoredDisplay::Show))
                    {
                        ApplyIgnoredDisplay(IgnoredDisplay::Show, true);
                    }

                    if (ImGui::MenuItem("Dimmed", nullptr,
                                        m_ignoredDisplay == IgnoredDisplay::Dim))
                    {
                        ApplyIgnoredDisplay(IgnoredDisplay::Dim, true);
                    }

                    if (ImGui::MenuItem("Hidden", nullptr,
                                        m_ignoredDisplay == IgnoredDisplay::Hide))
                    {
                        ApplyIgnoredDisplay(IgnoredDisplay::Hide, true);
                    }

                    ImGui::EndMenu();
                }

                ImGui::EndMenu();
            }

            ImGui::Separator();

            // A shortcut nobody can discover is not a shortcut, so Ctrl+Scroll and
            // Ctrl+0 both get a menu entry — all three feed SetEditorFontSize, the same
            // function the keyboard shortcuts in OnUIRender call, so the menu and the
            // keys cannot disagree about what these do.
            if (ImGui::MenuItem("Zoom In", "Ctrl+="))
                SetEditorFontSize(m_editorFontSize + kEditorFontSizeStep);

            if (ImGui::MenuItem("Zoom Out", "Ctrl+-"))
                SetEditorFontSize(m_editorFontSize - kEditorFontSizeStep);

            if (ImGui::MenuItem("Reset Zoom", "Ctrl+0"))
                SetEditorFontSize(kDefaultEditorFontSize);

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

    bool GraphiteLayer::AnyDocumentModified() const
    {
        for (std::size_t index = 0; index < m_documents.Count(); ++index)
        {
            const core::text::OpenItem& item = m_documents.At(index);

            if (item.document && item.document->IsModified())
                return true;
        }

        return false;
    }

    bool GraphiteLayer::OnCloseRequested()
    {
        // Save All having succeeded, or Discard All having been chosen, already
        // answered this - see FinishClose. Re-checking for dirty documents here would
        // reopen the modal on Discard All, which never touches document content, only
        // bypasses asking about it.
        if (m_closeApproved)
            return true;

        // The modal is already up and unresolved; RenderCloseConfirm owns it from here.
        if (m_pendingClosePurpose == ClosePurpose::ExitApplication)
            return false;

        if (!AnyDocumentModified())
            return true;

        m_pendingClosePurpose = ClosePurpose::ExitApplication;
        m_openCloseConfirmPopup = true;
        return false;
    }

    void GraphiteLayer::RequestCloseConfirmation(ClosePurpose purpose)
    {
        if (!AnyDocumentModified())
        {
            FinishClose(purpose);
            return;
        }

        m_pendingClosePurpose = purpose;
        m_openCloseConfirmPopup = true;
    }

    void GraphiteLayer::FinishClose(ClosePurpose purpose)
    {
        m_pendingClosePurpose.reset();

        switch (purpose)
        {
            case ClosePurpose::ExitApplication:
                // OnCloseRequested refusing earlier - to raise this very modal - cleared
                // Window's close flag for good; nothing re-raises it but this call, which
                // is what lets the main loop ask again next frame and actually break out
                // of it this time. See Window::CancelClose and Layer::OnCloseRequested.
                m_closeApproved = true;
                core::Application::Get().GetWindow().RequestClose();
                break;

            case ClosePurpose::CloseWorkspace:
                m_workspace.Close();
                break;
        }
    }

    void GraphiteLayer::RenderCloseConfirm()
    {
        if (m_openCloseConfirmPopup)
        {
            ImGui::OpenPopup(kUnsavedWorkTitle);
            m_openCloseConfirmPopup = false;
        }

        if (!ImGui::BeginPopupModal(kUnsavedWorkTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        ImGui::TextUnformatted("These files have unsaved changes:");
        ImGui::Separator();

        for (std::size_t index = 0; index < m_documents.Count(); ++index)
        {
            const core::text::OpenItem& item = m_documents.At(index);

            if (item.document && item.document->IsModified())
                ImGui::BulletText("%s", core::fs::ToUtf8(item.path.filename()).c_str());
        }

        ImGui::Separator();

        // m_pendingClosePurpose can only be unset here if the popup is somehow drawn
        // without RequestCloseConfirmation having raised it first, which never happens -
        // ExitApplication is a reasonable fallback all the same, since it is the more
        // conservative of the two actions FinishClose can take.
        const ClosePurpose purpose = m_pendingClosePurpose.value_or(ClosePurpose::ExitApplication);

        if (ImGui::Button("Save All"))
        {
            // A stopped batch and one that finished with a document skipped over a
            // conflict both come back false from SaveAllModified, and both mean the same
            // thing here: some dirty document is not actually saved, so closing now
            // would lose it exactly as a failed write would. Treating them alike is
            // deliberate - the status bar SaveAllModified itself sets already says which
            // one happened and to which file, and this modal simply stays up instead of
            // closing on a promise it did not keep, so the user can retry (a conflicted
            // file resolves itself once its own Ctrl+S is answered) or fall through to
            // Discard All / Cancel with the same picture in front of them.
            if (m_documentWindows.SaveAllModified())
            {
                FinishClose(purpose);
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Discard All"))
        {
            FinishClose(purpose);
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel"))
        {
            m_pendingClosePurpose.reset();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}
