#include "Core/Base/Base.h"

#include "App/GraphiteLayer.h"

#include "Editor/StatusBar.h"

#include "Panels/ConsolePanel.h"

#include "Panels/ExplorerPanel.h"

#include "Settings/AppSettings.h"

#include "App/GraphiteMenu.h"

#include "Core/App/Application.h"

#include "Core/Base/Log.h"

#include "Settings/EditorSection.h"
#include "Settings/KeyBindingsSection.h"
#include "Settings/SchemeSection.h"
#include "Settings/SettingsPanel.h"

#include "Core/Fs/FileSystem.h"

#include "Core/Platform/Process.h"

#include <GLFW/glfw3.h>

#include <imgui.h>

#include <cstddef>

#include <utility>

namespace Graphite {

	namespace {
		// The main window alone, not the process: a stale-content sweep only makes sense for
		// the window whose editors are on screen. ImGuiIO::AppFocusLost is not this — the
		// backend sets it on focus *loss*, so it fires as the user leaves rather than as they
		// come back.
		bool MainWindowHasFocus()
		{
			auto* handle =
				static_cast<GLFWwindow*>(Core::Application::Get().GetWindow().GetNativeHandle());

			return glfwGetWindowAttrib(handle, GLFW_FOCUSED) != 0;
		}
	}

	GraphiteLayer::GraphiteLayer(Core::Ref<Core::RingBufferSink> sink,
								 std::filesystem::path initialFolder)
		: Core::Layer("Ide"), m_Sink(std::move(sink)), m_InitialFolder(std::move(initialFolder)),
		  m_CloseQuestion(m_Modals, kUnsavedWorkTitle)
	{
		m_Layout.Left = {"files"};
		m_Layout.Bottom = {"console"};
		m_Layout.Center = {"settings"};

		// Only when the caption is ours to draw: the window is allowed to refuse to hide the
		// system one, and then TitleBar::Render draws nothing and the taller row would be
		// empty. The layer is constructed after Window::Create, so the window can answer.
		m_CustomTitleBar = Core::Application::Get().GetWindow().HasCustomTitleBar();

		if (m_CustomTitleBar)
		{
			m_Layout.MenuBarHeight = kCaptionHeight;
		}
	}

	void GraphiteLayer::OnAttach()
	{
		auto* explorer = static_cast<ExplorerPanel*>(
			m_Panels.Add(Core::CreateScope<ExplorerPanel>(m_Workspace, m_Documents)));

		m_Panels.Add(Core::CreateScope<ConsolePanel>(m_Sink));

		m_Workspace.RegisterCommands(m_Commands);
		m_DocumentWindows.RegisterCommands(m_Commands, m_Keys);
		m_EditorSettings.RegisterCommands(m_Commands, m_Keys);
		explorer->RegisterCommands(m_Commands);
		m_Panels.RegisterCommands(m_Commands);
		RegisterApplicationCommands();

		// After every default registered above, so an override shadows the binding it
		// replaces rather than being shadowed by it.
		AppSettings::LoadKeyBindings(m_Keys);

		std::vector<Core::Scope<SettingsSection>> sections;

		sections.push_back(Core::CreateScope<EditorSection>(m_EditorSettings, m_Commands));
		sections.push_back(Core::CreateScope<SchemeSection>(m_ActiveScheme, m_Modals));
		sections.push_back(Core::CreateScope<KeyBindingsSection>(
			m_Commands, m_Keys, [this] { AppSettings::SaveKeyBindings(m_Keys); }));

		m_Panels.Add(Core::CreateScope<SettingsPanel>(std::move(sections)));

		ReportConflictingKeyBindings();
		SubscribeToWorkspaceEvents();

		APP_INFO("Registered panels: {}", m_Panels.GetCount());

		explorer->LoadSettings();

		m_EditorSettings.Load();
		m_ActiveScheme.Load();

		OpenStartupFolder();

		// Last, after every command there is: the tree names ids, so one built earlier would
		// draw a bar of disabled entries showing raw ids.
		m_MenuTree = BuildMenuTree();
	}

	void GraphiteLayer::RegisterApplicationCommands()
	{
		// "Workspace" rather than "Folder": closing drops the solution and the selection
		// along with the folder.
		m_Commands.Add({.Id = "workspace.close",
						.Label = "Close Workspace",
						.Run = [this] { RequestCloseConfirmation(ClosePurpose::CloseWorkspace); },
						.Enabled = [this] { return m_Workspace.IsOpen(); }});

		// Through the window's close flag, not Application::Close(): the main loop only asks
		// a layer's OnCloseRequested when Window::ShouldClose() is true, and Close() alone
		// never sets it, so this is what puts Exit through the same question as Alt+F4.
		//
		// Alt+F4 is in the label rather than in the keymap: it is Windows' shortcut, and a
		// binding for it would advertise something that cannot be rebound.
		m_Commands.Add({.Id = "app.exit",
						.Label = "Exit\tAlt+F4",
						.Run = [] { Core::Application::Get().GetWindow().RequestClose(); }});

		m_Commands.Add({.Id = "view.resetLayout",
						.Label = "Reset Layout",
						.Run = [this] {
							m_Panels.ResetVisibility();
							m_DockSpace.RequestLayoutReset();
							APP_INFO("Layout restored to the default");
						}});

		m_Commands.Add({.Id = "help.logTestEntries",
						.Label = "Log test entries",
						.Run = [] {
							APP_TRACE("Diagnostic entry");
							APP_INFO("Informational entry");
							APP_WARN("Warning entry");
							APP_ERROR("Sample error");
						}});
	}

	void GraphiteLayer::ReportConflictingKeyBindings()
	{
		// The defaults are assembled across five files, each of which believes it is claiming
		// a free chord. An error rather than an assertion: a duplicate binding is a wiring
		// mistake worth shouting about, not a reason to refuse to start.
		for (const Core::Conflict& conflict : m_Keys.FindConflicts())
		{
			APP_ERROR("'{}' and '{}' are both bound to {}", conflict.First, conflict.Second,
					  conflict.Chord.ToString());
		}
	}

	void GraphiteLayer::SubscribeToWorkspaceEvents()
	{
		m_Events.Subscribe<OpenFileRequested>([this](const OpenFileRequested& event) {
			m_DocumentWindows.RequestOpen(event.Path, event.Permanent);
		});

		m_Events.Subscribe<WorkspaceClosedEvent>(
			[this](const WorkspaceClosedEvent&) { m_Documents.CloseAll(); });

		m_Events.Subscribe<WorkspaceRootMovedEvent>([this](const WorkspaceRootMovedEvent& event) {
			m_DocumentWindows.HandleRootMoved(event.From, event.To);
		});

		m_Events.Subscribe<WorkspaceFilesChangedEvent>(
			[this](const WorkspaceFilesChangedEvent& event) {
				m_DocumentWindows.HandleFilesChanged(event.Directories, event.Everything);
			});
	}

	void GraphiteLayer::OpenStartupFolder()
	{
		std::filesystem::path startFolder = m_InitialFolder;

		if (startFolder.empty())
			startFolder = AppSettings::LoadLastFolder();

		if (!startFolder.empty())
			(void)m_Workspace.OpenFolder(startFolder);
	}

	void GraphiteLayer::OnUpdate(Core::Timestep ts)
	{
		m_Workspace.Update();
		m_Panels.OnUpdate(ts);

		UpdateFocusReload(ts);

		// Watchers drop events — buffer overflow, or a tool touching many files at once — and
		// returning from another application is exactly when that matters.
		m_DocumentWindows.NotifyWindowFocus(MainWindowHasFocus());
	}

	void GraphiteLayer::UpdateFocusReload(Core::Timestep ts)
	{
		// The process, not the main window: a panel dragged out into its own viewport is a
		// separate OS window, and working in one is not having left the application.
		if (!Core::IsForeground())
		{
			m_SecondsInBackground += ts.GetSeconds();
			m_AppForeground = false;
			return;
		}

		if (!m_AppForeground)
		{
			m_AppForeground = true;

			// Reload does nothing without an open folder, but the trace below would still
			// claim a reload happened.
			if (m_SecondsInBackground >= kReloadAfterSecondsAway && m_Workspace.IsOpen())
			{
				APP_TRACE("Reloading the workspace after {:.1f}s away", m_SecondsInBackground);
				m_Workspace.Reload();
			}

			m_SecondsInBackground = 0.0f;
		}
	}

	void GraphiteLayer::OnImGuiRender()
	{
		DrawStatusBar(m_Documents, m_Workspace.GetRoot(), m_DocumentWindows.GetStatusMessage());

		m_DockSpace.Begin(m_Layout);
		m_MenuBar.Render(m_Commands, m_Keys, m_MenuTree, m_Layout.MenuBarHeight,
						 [this] { m_TitleBar.Render(Core::Application::Get().GetWindow()); });
		m_DockSpace.End();

		// Outside the dockspace host's Begin/End above, and outside every other window: a
		// modal popup must not be opened from inside a window that may not be current when
		// the popup needs to draw.
		RenderCloseConfirm();

		// Panels render outside the host window — inside it they would dock as ordinary child
		// windows instead of landing in the dockspace nodes.
		m_Panels.OnImGuiRender();

		const CommandContext commandContext{&m_Commands, &m_Keys, &m_Router, &m_FrameInput};

		m_DocumentWindows.Render(commandContext, m_EditorSettings.GetFontSize(),
								 m_EditorSettings.GetIndentStyle(), m_DockSpace.GetCenterNodeId(),
								 m_EditorSettings.IsWordWrap(), m_ActiveScheme.Get());

		// The last point in the frame that reads the queue: what a frame drains is exactly
		// what arrived since the previous one was drawn.
		m_FrameInput.Clear();
	}

	void GraphiteLayer::OnEvent(Core::Event& event)
	{
		// The panels first, and only those that drew: a chord being recorded is consumed
		// there, so the router never sees it. Capturing Ctrl+Shift+S would otherwise bind it
		// and save everything in the same keystroke.
		m_Panels.OnEvent(event);

		if (event.Handled)
		{
			return;
		}

		RouteShortcuts(event);

		if (event.Handled)
		{
			return;
		}

		QueueForFocusedEditor(event);
	}

	void GraphiteLayer::RouteShortcuts(Core::Event& event)
	{
		Core::EventDispatcher dispatcher{event};

		dispatcher.Dispatch<Core::KeyPressedEvent>([this](Core::KeyPressedEvent& pressed) {
			return m_Router.OnKeyPressed(m_Commands, m_Keys, pressed);
		});

		dispatcher.Dispatch<Core::KeyReleasedEvent>([this](Core::KeyReleasedEvent& released) {
			return m_Router.OnKeyReleased(m_Commands, m_Keys, released);
		});

		// A modifier held for a Ctrl+click is not a tap, however it is released afterwards.
		dispatcher.Dispatch<Core::MouseButtonPressedEvent>([this](Core::MouseButtonPressedEvent&) {
			m_Router.CancelTap();
			return false;
		});
	}

	void GraphiteLayer::QueueForFocusedEditor(Core::Event& event)
	{
		// Queued rather than handed to an editor here: which document holds the keyboard is
		// decided by ImGui's window focus, which is only knowable while drawing, and by then
		// the event object is long gone.
		//
		// Both handlers return false: the queue is read by at most one focused editor, and
		// often by none at all, so reporting the key as consumed would claim it landed
		// somewhere.
		Core::EventDispatcher dispatcher{event};

		dispatcher.Dispatch<Core::KeyPressedEvent>([this](Core::KeyPressedEvent& pressed) {
			m_FrameInput.PushKey(pressed.GetKeyCode(), pressed.GetModifiers(), pressed.IsRepeat());
			return false;
		});

		dispatcher.Dispatch<Core::KeyTypedEvent>([this](Core::KeyTypedEvent& typed) {
			m_FrameInput.PushCharacter(typed.GetCodepoint());
			return false;
		});
	}

	bool GraphiteLayer::OnCloseRequested()
	{
		// Save All having succeeded, or Discard All having been chosen, already answered this
		// — see FinishClose. Re-checking for dirty documents here would reopen the modal on
		// Discard All, which never touches document content.
		if (m_CloseApproved)
			return true;

		// Any unresolved close question, not only this purpose's: "Close Workspace" asks the
		// same question, and Alt+F4 reaches here regardless of what ImGui thinks holds input
		// focus, so testing the purpose would overwrite a pending workspace close with an
		// application exit under a dialog the user is already reading.
		if (m_CloseQuestion.IsAsked())
		{
			return false;
		}

		if (!m_DocumentWindows.HasModifiedDocuments())
		{
			return true;
		}

		m_CloseQuestion.Ask(ClosePurpose::ExitApplication);
		return false;
	}

	void GraphiteLayer::RequestCloseConfirmation(ClosePurpose purpose)
	{
		if (!m_DocumentWindows.HasModifiedDocuments())
		{
			FinishClose(purpose);
			return;
		}

		// One close question at a time, whatever raised it. Ask queues rather than replaces,
		// so without this a second "Close Workspace" from the menu would close the workspace
		// twice, once per answer.
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
			// OnCloseRequested refusing earlier — to raise this very modal — cleared Window's
			// close flag for good, and nothing but this call re-raises it.
			m_CloseApproved = true;
			Core::Application::Get().GetWindow().RequestClose();
			break;

		case ClosePurpose::CloseWorkspace:
			m_Workspace.Close();
			break;
		}
	}

	bool GraphiteLayer::ConfirmationStillNeeded(ClosePurpose purpose)
	{
		if (m_DocumentWindows.HasModifiedDocuments())
		{
			return true;
		}

		// The question went stale while it waited its turn, the request behind it did not:
		// the user asked to close and nothing now stands in the way.
		FinishClose(purpose);
		return false;
	}

	void GraphiteLayer::ListModifiedDocuments() const
	{
		for (std::size_t index = 0; index < m_Documents.GetCount(); ++index)
		{
			const Core::OpenItem& item = m_Documents.At(index);

			if (item.Document && item.Document->IsModified())
				ImGui::BulletText("%s", Core::ToUtf8(item.Path.filename()).c_str());
		}
	}

	GraphiteLayer::ClosePurpose GraphiteLayer::AskedPurposeOrExit() const
	{
		const ClosePurpose* asked = m_CloseQuestion.GetCurrent();

		return asked != nullptr ? *asked : ClosePurpose::ExitApplication;
	}

	void GraphiteLayer::RenderCloseConfirm()
	{
		if (!m_CloseQuestion.Begin([this](ClosePurpose purpose) {
				return ConfirmationStillNeeded(purpose);
			}))
		{
			return;
		}

		ImGui::TextUnformatted("These files have unsaved changes:");
		ImGui::Separator();

		// Listed afresh every frame, so a Save All that saved some and not others shows what
		// it actually left behind.
		ListModifiedDocuments();

		ImGui::Separator();

		const ClosePurpose purpose = AskedPurposeOrExit();

		if (ImGui::Button("Save All"))
		{
			// A stopped batch and one that skipped a conflicted document both come back
			// false, and both mean some dirty document is not on disk, so the modal stays up
			// rather than closing on a promise it did not keep. SaveAllModified has already
			// put which one happened in the status bar.
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
