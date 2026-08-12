#include "GraphiteLayer.h"

#include "Editor/StatusBar.h"
#include "Panels/ConsolePanel.h"
#include "Panels/ExplorerPanel.h"
#include "AppSettings.h"
#include "GraphiteMenu.h"

#include "Core/App/Application.h"
#include "Core/Base/Log.h"
#include "Core/Command/KeyBindingsPanel.h"
#include "Core/Fs/FileSystem.h"
#include "Core/Platform/Process.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <cstddef>
#include <utility>

namespace Graphite
{
    namespace
    {
        constexpr const char* kUnsavedWorkTitle = "Unsaved changes##ExitUnsavedWork";
    }

    GraphiteLayer::GraphiteLayer(std::shared_ptr<Core::RingBufferSink> sink,
                       std::filesystem::path initialFolder)
        : Core::Layer("Ide"), m_Sink(std::move(sink)), m_InitialFolder(std::move(initialFolder)),
          m_CloseQuestion(m_Modals, kUnsavedWorkTitle)
    {
        m_Layout.left = {"files"};
        m_Layout.bottom = {"console"};
        m_Layout.center = {"keybindings"};

        // 32 px is what Windows itself uses for a caption, so the drawn window buttons
        // keep their familiar hit size — see TitleBar.h. DockSpaceHost::Begin is what
        // actually applies this, at the one moment it can (its own host window's Begin()).
        //
        // Only when the caption is actually ours to draw. WindowFrame::Hide is allowed to
        // refuse — a non-Windows build, a subclass that did not take — and then
        // TitleBar::Render draws nothing, so the row would be twice the height it needs,
        // empty on the right, underneath the system title bar it failed to remove: worse
        // than the menu bar this branch started from. The layer is constructed in
        // OnInit(), after Window::Create, so the window can already answer.
        m_CustomTitleBar = Core::Application::Get().GetWindow().HasCustomTitleBar();

        if (m_CustomTitleBar)
        {
            m_Layout.menuBarHeight = kCaptionHeight;
        }
    }

    void GraphiteLayer::OnAttach()
    {
        auto* explorer = static_cast<ExplorerPanel*>(
            m_Panels.Add(std::make_unique<ExplorerPanel>(m_Workspace, m_Documents)));

        m_Panels.Add(std::make_unique<ConsolePanel>(m_Sink));

        m_Workspace.RegisterCommands(m_Commands);
        m_DocumentWindows.RegisterCommands(m_Commands, m_Keys);
        m_EditorSettings.RegisterCommands(m_Commands, m_Keys);
        explorer->RegisterCommands(m_Commands);
        m_Panels.RegisterCommands(m_Commands);

        // "Workspace" rather than "Folder": closing drops the solution and the selection
        // along with the folder, and the menu offers two ways to open one. Naming only
        // the folder would leave the other two looking like they survive.
        m_Commands.Add({.id = "workspace.close",
                        .label = "Close Workspace",
                        .run = [this] { RequestCloseConfirmation(ClosePurpose::CloseWorkspace); },
                        .enabled = [this] { return m_Workspace.IsOpen(); }});

        // Through the window's close flag, not Application::Close() directly:
        // Application's main loop only asks a layer's OnCloseRequested when
        // Window::ShouldClose() is true, and Close() alone never sets it. Routing through
        // RequestClose() puts Exit through the exact same question as the OS close button
        // and Alt+F4.
        //
        // "Alt+F4" is in the label rather than in the keymap: it is Windows' shortcut, not
        // ours, and a binding for it would advertise something that cannot be rebound.
        m_Commands.Add({.id = "app.exit",
                        .label = "Exit\tAlt+F4",
                        .run = [] { Core::Application::Get().GetWindow().RequestClose(); }});

        m_Commands.Add({.id = "view.resetLayout",
                        .label = "Reset Layout",
                        .run = [this]
                        {
                            m_Panels.ResetVisibility();
                            m_DockSpace.RequestLayoutReset();
                            CORE_INFO("Layout restored to the default");
                        }});

        m_Commands.Add({.id = "help.logTestEntries",
                        .label = "Log test entries",
                        .run = []
                        {
                            CORE_TRACE("Diagnostic entry");
                            CORE_INFO("Informational entry");
                            CORE_WARN("Warning entry");
                            CORE_ERROR("Sample error");
                        }});

        // After the defaults are registered and before the menu is built: overrides
        // shadow defaults, and the menu reads the binding in force when it draws each
        // item's shortcut.
        AppSettings::LoadKeyBindings(m_Keys);

        // After the load, so the screen opens onto the bindings actually in force. The
        // callback is what keeps Core clear of AppSettings: the panel knows a binding
        // moved, this layer knows where bindings are kept.
        m_Panels.Add(std::make_unique<Core::KeyBindingsPanel>(
            m_Commands, m_Keys, [this] { AppSettings::SaveKeyBindings(m_Keys); }));

        // The defaults are assembled across five files, each of which believes it is
        // claiming a free chord, and nothing but this notices when two of them claim the
        // same one. An error rather than an assertion: a duplicate binding is a wiring
        // mistake worth shouting about, not a reason to refuse to start.
        for (const Core::Conflict& conflict : m_Keys.FindConflicts())
        {
            CORE_ERROR("'{}' and '{}' are both bound to {}", conflict.first, conflict.second,
                       conflict.chord.ToString());
        }

        m_Events.Subscribe<OpenFileRequested>(
            [this](const OpenFileRequested& event)
            {
                m_DocumentWindows.RequestOpen(event.path, event.permanent);
            });

        m_Events.Subscribe<WorkspaceClosedEvent>([this](const WorkspaceClosedEvent&)
                                                 { m_Documents.CloseAll(); });

        m_Events.Subscribe<WorkspaceRootMovedEvent>(
            [this](const WorkspaceRootMovedEvent& event)
            { m_DocumentWindows.HandleRootMoved(event.from, event.to); });

        m_Events.Subscribe<WorkspaceFilesChangedEvent>(
            [this](const WorkspaceFilesChangedEvent& event)
            { m_DocumentWindows.HandleFilesChanged(event.directories, event.everything); });

        CORE_INFO("Registered panels: {}", m_Panels.GetCount());

        explorer->LoadSettings();

        m_EditorSettings.Load();

        std::filesystem::path startFolder = m_InitialFolder;
        if (startFolder.empty())
            startFolder = AppSettings::LoadLastFolder();

        if (!startFolder.empty())
            (void)m_Workspace.OpenFolder(startFolder);

        // Last, after every RegisterCommands above: the tree names ids, so one built
        // earlier would draw a bar of disabled entries showing raw ids.
        m_MenuTree = BuildMenuTree();
    }

    void GraphiteLayer::OnUpdate(Core::Timestep ts)
    {
        m_Workspace.Update();
        m_Panels.OnUpdate(ts);

        // In its own function so its early return cannot swallow the calls above. An
        // earlier revision had this logic inlined here and dropped the panel update
        // silently — nothing failed, because no panel overrides it yet.
        UpdateFocusReload(ts);

        // Watchers drop events — buffer overflow, or a tool touching many files at once —
        // and returning from another application is exactly when that matters. Reading
        // the OS-level flag directly (rather than reusing Core::IsForeground(),
        // which UpdateFocusReload above already does) is deliberate: that one answers for
        // the whole process, true for a panel dragged into its own viewport just as much
        // as for the main window, where a stale-content sweep only makes sense for the
        // window whose editors are actually on screen. ImGuiIO::AppFocusLost is not this:
        // it is a one-frame flag the backend sets on focus *loss*, so it would fire the
        // sweep as the user leaves rather than as they return.
        auto* handle = static_cast<GLFWwindow*>(Core::Application::Get().GetWindow().GetNativeHandle());
        m_DocumentWindows.NotifyWindowFocus(glfwGetWindowAttrib(handle, GLFW_FOCUSED) != 0);
    }

    void GraphiteLayer::UpdateFocusReload(Core::Timestep ts)
    {
        // The process, not the main window: a panel dragged out into its own viewport is
        // a separate OS window, and working in one used to read as having left the
        // application — so coming back to the main window announced a return from an
        // absence that never happened.
        if (!Core::IsForeground())
        {
            m_SecondsInBackground += ts.GetSeconds();
            m_AppForeground = false;
            return;
        }

        // Only the moment focus returns, not every frame it is held.
        if (!m_AppForeground)
        {
            m_AppForeground = true;

            // Reload itself does nothing without an open folder, but the log line would
            // still claim a reload happened. A trace that reports work nobody did is
            // worse than no trace at all — it is the sort of thing someone later spends
            // an afternoon believing.
            if (m_SecondsInBackground >= kReloadAfterSecondsAway && m_Workspace.IsOpen())
            {
                CORE_TRACE("Reloading the workspace after {:.1f}s away", m_SecondsInBackground);
                m_Workspace.Reload();
            }

            m_SecondsInBackground = 0.0f;
        }
    }

    void GraphiteLayer::OnUIRender()
    {
        DrawStatusBar(m_Documents, m_Workspace.GetRoot(), m_DocumentWindows.GetStatusMessage());

        m_DockSpace.Begin(m_Layout);
        m_MenuBar.Render(m_Commands, m_Keys, m_MenuTree, m_Layout.menuBarHeight,
                         [this] { m_TitleBar.Render(Core::Application::Get().GetWindow()); });
        m_DockSpace.End();

        // Never nested inside the dockspace host's Begin/End above, or any other
        // window's - see RenderCloseConfirm's own comment for why that matters to a
        // modal popup.
        RenderCloseConfirm();

        // Panels render outside the host window — inside it they would dock as ordinary
        // child windows instead of landing in the dockspace nodes.
        m_Panels.OnUIRender();

        // The context rather than three arguments: the widget that observes a Ctrl+wheel
        // gesture is the one place that knows the gesture happened over an editor, and it
        // is drawn from here, so what it needs to turn that into a command is passed down
        // rather than stored anywhere on the way.
        const CommandContext commandContext{&m_Commands, &m_Keys, &m_Router};

        m_DocumentWindows.Render(commandContext, m_EditorSettings.GetFontSize(),
                                 m_EditorSettings.GetIndentStyle(), m_DockSpace.GetCenterNodeId(),
                                 m_EditorSettings.IsWordWrap());

        // After every window this frame has Begin/End'd, which is what
        // ImGuiInputFlags_RouteGlobal expects — there is no "current" window whose focus a
        // RouteFocused check could compare against at this point. The chords themselves
        // are the keymap's, registered alongside the commands they run, so a key press and
        // the menu item showing the same combination cannot drift apart.
        //
        // Not while a panel is reading raw keys: a chord pressed to record it would be
        // bound and run in the same frame, so capturing Ctrl+Shift+S would also save
        // everything. Asked of the panels as a whole, so this knows nothing about which
        // one captures.
        //
        // Known and accepted: skipping the call also skips the routing registrations
        // Dispatch's own comment describes, so the first frame after a capture ends can
        // miss a chord. It costs one keypress in a situation the user just created on
        // purpose, and the alternative is a router that can be told to observe without
        // running — more machinery than the fault is worth.
        if (!m_Panels.IsCapturingInput())
        {
            m_Router.Dispatch(m_Commands, m_Keys);
        }
    }

    bool GraphiteLayer::OnCloseRequested()
    {
        // Save All having succeeded, or Discard All having been chosen, already
        // answered this - see FinishClose. Re-checking for dirty documents here would
        // reopen the modal on Discard All, which never touches document content, only
        // bypasses asking about it.
        if (m_CloseApproved)
            return true;

        // Some close question is already up and unresolved - not necessarily this one.
        // "Close Workspace" reaches here too: it asks the same question with
        // CloseWorkspace as its purpose, and the OS close button / Alt+F4
        // reach Window::ShouldClose() regardless of what ImGui thinks has input focus,
        // modal or not. Testing `== ExitApplication` here would fall through on a
        // pending CloseWorkspace question and silently overwrite it below - the user
        // would be looking at a dialog they opened from the menu, believing Save All
        // will close the workspace, while it would actually exit the application.
        // Refusing until whichever question is already open resolves is the
        // conservative choice; the user can just close again afterwards. This is
        // deliberately the wider check, not an oversight - and wider still than "on
        // screen": a close question waiting its turn behind another is every bit as
        // unanswered as one the user is looking at.
        if (m_CloseQuestion.IsAsked())
        {
            return false;
        }

        if (!m_DocumentWindows.HasAnyModified())
        {
            return true;
        }

        // Asked even when another question - one of DocumentWindows' four - is on
        // screen, which Alt+F4 and the OS close button reach past regardless of ImGui
        // holding input. ModalQuestion asks the gate before opening anything, so this is
        // a question queued rather than a modal stacked, and its being asked at all is
        // what keeps this refusing until it is answered.
        m_CloseQuestion.Ask(ClosePurpose::ExitApplication);
        return false;
    }

    void GraphiteLayer::RequestCloseConfirmation(ClosePurpose purpose)
    {
        if (!m_DocumentWindows.HasAnyModified())
        {
            FinishClose(purpose);
            return;
        }

        // The same refusal OnCloseRequested makes, and for the same reason: one close
        // question at a time, whatever raised it. Ask queues rather than replaces, so
        // without this a second "Close Workspace" from the menu would put a second
        // question behind the first and close the workspace twice - once per answer.
        if (m_CloseQuestion.IsAsked())
        {
            return;
        }

        m_CloseQuestion.Ask(purpose);
    }

    void GraphiteLayer::FinishClose(ClosePurpose purpose)
    {
        switch (purpose)
        {
            case ClosePurpose::ExitApplication:
                // OnCloseRequested refusing earlier - to raise this very modal - cleared
                // Window's close flag for good; nothing re-raises it but this call, which
                // is what lets the main loop ask again next frame and actually break out
                // of it this time. See Window::CancelClose and Layer::OnCloseRequested.
                m_CloseApproved = true;
                Core::Application::Get().GetWindow().RequestClose();
                break;

            case ClosePurpose::CloseWorkspace:
                m_Workspace.Close();
                break;
        }
    }

    void GraphiteLayer::RenderCloseConfirm()
    {
        // The question waits for as long as another holds the gate, so an exit asked for
        // while a per-tab or reload question is up is put on screen the moment that one
        // resolves rather than being dropped - and dropping it would matter twice over,
        // since an unanswered question is what makes OnCloseRequested refuse, and nothing
        // but this modal ever answers it. A popup dismissed with Escape, or destroyed,
        // counts as Cancel for the same reason: left asked, it would make OnCloseRequested
        // refuse every close for the rest of the session and the window could then only be
        // force-quit. Begin does both.
        // Worth asking only while something is still unsaved. A question raised over dirty
        // documents can reach the front of the queue after they have been dealt with one at
        // a time - a per-tab close question answered with Save, say - and asking then would
        // put "These files have unsaved changes:" on screen above an empty list. Checked
        // when it comes off the queue rather than when it joined, since that is exactly the
        // interval in which the answer changes.
        //
        // The close still happens. What went stale is the question, not the request behind
        // it: the user asked to exit and nothing now stands in the way, which is the case
        // RequestCloseConfirmation already closes outright when it finds no dirty document
        // to ask about. Dropping the question alone would leave the application refusing an
        // exit it has no reason left to refuse.
        const auto stillWorthAsking = [this](ClosePurpose purpose)
        {
            if (m_DocumentWindows.HasAnyModified())
            {
                return true;
            }

            FinishClose(purpose);
            return false;
        };

        if (!m_CloseQuestion.Begin(stillWorthAsking))
        {
            return;
        }

        ImGui::TextUnformatted("These files have unsaved changes:");
        ImGui::Separator();

        // Recomputed every frame this modal is open, deliberately not snapshotted once
        // when it opens: a failed Save All can leave some documents saved and others
        // not (see the comment on the Save All button below), and the list here should
        // reflect that partial progress if the user looks again or retries, rather than
        // continuing to name a file that already saved cleanly. Recomputing also cannot
        // go stale the way a snapshot taken once at open time could.
        for (std::size_t index = 0; index < m_Documents.GetCount(); ++index)
        {
            const Core::OpenItem& item = m_Documents.At(index);

            if (item.document && item.document->IsModified())
                ImGui::BulletText("%s", Core::ToUtf8(item.path.filename()).c_str());
        }

        ImGui::Separator();

        // There can only be no purpose here if the popup is somehow drawn without
        // RequestCloseConfirmation or OnCloseRequested having asked first, which never
        // happens - ExitApplication is a reasonable fallback all the same, since it is the
        // more conservative of the two actions FinishClose can take.
        const ClosePurpose* asked = m_CloseQuestion.GetCurrent();
        const ClosePurpose purpose = asked != nullptr ? *asked : ClosePurpose::ExitApplication;

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
            if (m_DocumentWindows.SaveAllModified())
            {
                FinishClose(purpose);
                m_CloseQuestion.Answer();
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Discard All"))
        {
            FinishClose(purpose);
            m_CloseQuestion.Answer();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel"))
        {
            m_CloseQuestion.Answer();
        }

        m_CloseQuestion.End();
    }
}
