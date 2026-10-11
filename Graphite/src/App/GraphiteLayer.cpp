#include "App/GraphiteLayer.h"

#include "Core/Base/Base.h"

#include "Editor/ActiveTextDocument.h"
#include "Editor/EditorCommands.h"
#include "Theme/Palettes.h"
#include "UI/ClipboardCopy.h"
#include "Editor/StatusBar/ServerStatusText.h"
#include "Editor/StatusBar/StatusBar.h"
#include "Editor/StatusBar/StatusLineComposer.h"
#include "Editor/StatusBar/StatusMessageEvent.h"
#include "Editor/TextEditorWidget.h"
#include "Language/Install/InstallSummary.h"
#include "Language/Install/ServerInstallActions.h"
#include "Language/Registry/LanguageRegistry.h"
#include "Language/Server/DotnetRuntime.h"
#include "Language/Server/ServerCatalog.h"

#include "Git/Changes/ChangesPanel.h"
#include "Theme/GitPalette.h"
#include "Git/GitStatusText.h"
#include "Git/GitView.h"
#include "Git/History/HistoryPlaceholder.h"
#include "Git/WriteFailureTitle.h"

#include "Console/ConsolePanel.h"

#include "Explorer/ExplorerPanel.h"

#include "Language/Diagnostics/ProblemsPanel.h"
#include "Search/ReferencesPanel.h"
#include "Sessions/SessionsPanel.h"
#include "Terminal/TerminalWindows.h"
#include "Persistence/RestrictedRestore.h"

#include <format>
#include <optional>

#include "Search/SearchPanel.h"

#include "Settings/AppSettings.h"

#include "App/GraphiteMenu.h"
#include "App/LanguageServerChoice.h"
#include "App/SampleToasts.h"
#include "App/SettingsPages.h"

#include "App/Version.h"
#include "Core/App/Application.h"
#include "Core/Network/HttpClient.h"
#include "Core/App/Redraw.h"

#include "Core/Base/Log.h"

#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/ToastStyle.h"
#include "Core/ImGui/UiScale.h"
#include "Core/ImGui/Theme.h"

#include "Theme/ChromePalette.h"
#include "Theme/SessionPalette.h"
#include "Theme/ToastPalette.h"

#include "UI/RelativePaths.h"

#include "Core/Events/ApplicationEvent.h"

#include "Settings/Panel/SettingsPanel.h"
#include "UI/DialogLayout.h"
#include "UI/LanguageSupportBar.h"
#include "UI/RestrictedModeBar.h"
#include "UI/ToastEvent.h"
#include "Workspace/WorkspaceTrust.h"

#include "Core/FileSystem/FileSystem.h"

#include "Core/Platform/Process.h"
#include "Core/Platform/Shell.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>

#include <chrono>
#include <cstddef>

#include <string>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {
		// The main window alone, not the process: a stale-content sweep only makes sense for
		// the window whose editors are on screen. ImGuiIO::AppFocusLost is not this — the
		// backend sets it on focus *loss*, so it fires as the user leaves rather than as they
		// come back.
		bool MainWindowHasFocus()
		{
			return Core::Application::Get().GetWindow().IsFocused();
		}

		std::optional<ServerJob> ServerJobFor(const ServerInstalls& installs, const ServerDefinition& server)
		{
			const std::optional<InstallJobKind> running = installs.RunningJobOf(server.Id);

			if (!running || *running == InstallJobKind::CheckForUpdates)
			{
				return std::nullopt;
			}

			const std::optional<InstallProgress> latest = installs.ProgressOf(server.Id);

			return ServerJob{.Kind = *running, .Server = std::string{server.DisplayName}, .Percent = latest ? InstallPercent(*latest) : std::nullopt};
		}

		Core::ToastKind ToastKindOf(ServerMessageKind kind)
		{
			switch (kind)
			{
			case ServerMessageKind::Error:
				return Core::ToastKind::Error;
			case ServerMessageKind::Warning:
				return Core::ToastKind::Warning;
			}

			return Core::ToastKind::Error;
		}
	}

	GraphiteLayer::GraphiteLayer(Core::Ref<Core::RingBufferSink> sink,
								 std::filesystem::path initialFolder, AppSettings& settings)
		: Core::Layer("Ide"), m_Sink(std::move(sink)), m_InitialFolder(std::move(initialFolder)),
		  m_AppSettings(settings),
		  m_CloseQuestion(m_Modals, kUnsavedWorkTitle),
		  m_DropQuestion(m_Modals, kReplaceWorkspaceTitle),
		  m_About(m_Modals),
		  m_TrustParentQuestion(m_Modals, kTrustParentTitle)
	{
		m_Layout.Left = {"files", "changes"};

		// Beside the console rather than floating: a results list is something you read while
		// looking at the code it points into, which is what the bottom row is for. Without an
		// entry here a panel has nowhere to land and opens as a window of its own.
		m_Layout.Bottom = {"console", "problems", "search", "references"};
		m_Layout.Center = {"settings", "git", "sessions"};

		// Only when the caption is ours to draw: the window is allowed to refuse to hide the
		// system one, and then TitleBar::Render draws nothing and the taller row would be
		// empty. The layer is constructed after Window::Create, so the window can answer.
		m_CustomTitleBar = Core::Application::Get().GetWindow().HasCustomTitleBar();
	}

	void GraphiteLayer::OnAttach()
	{
		Core::ImGuiRenderer::SetInterfaceScale(
			static_cast<float>(m_AppSettings.GetInterfaceScalePercent()) / 100.0f);

		auto* explorer = m_Panels.Add(Core::CreateScope<ExplorerPanel>(m_Workspace, m_Documents, m_AppSettings, m_Events));
		m_Explorer = explorer;

		m_Panels.Add(Core::CreateScope<ConsolePanel>(m_Sink, m_Events));

		m_SearchPanel = m_Panels.Add(Core::CreateScope<SearchPanel>(
			m_Search, m_SearchSettings, m_Workspace, m_Documents, m_ActiveScheme,
			m_EditorSettings, m_PreviewSemantics));

		m_ReferencesPanel = m_Panels.Add(
			Core::CreateScope<ReferencesPanel>(m_Workspace, m_Documents, m_ActiveScheme,
											   m_EditorSettings, m_PreviewSemantics));

		m_Panels.Add(Core::CreateScope<ProblemsPanel>(m_Diagnostics, m_Documents, m_ActiveScheme));

		// Permanent, not a preview: pressing a button to open the file is as deliberate as
		// choosing "Open File" from the Changes panel's menu.
		m_DiffWindows.OpenFile = [this](const std::filesystem::path& path) {
			m_DocumentWindows.RequestOpen(path, true);
		};

		m_DiffWindows.OnCopied = [this](std::string_view text) {
			ReportCopied(m_Events, "selection", text);
		};

		m_DiffWindows.OnSetWordWrap = [this](bool wrap) { m_EditorSettings.SetDiffWordWrap(wrap); };

		// m_Toasts.OnCopied is left unset: Copy in a toast's menu only fills the clipboard. A
		// "Copied" toast raised by copying a toast would be a toast about a toast.

		auto* gitView = m_Panels.Add(Core::CreateScope<GitView>(m_Git, m_ActiveScheme, m_EditorSettings,
																m_DiffWindows, m_Modals, m_DocumentBuffers,
																m_Events));
		gitView->Subscribe();

		m_Panels.Add(Core::CreateScope<ChangesPanel>(m_Git, m_Modals, m_Documents,
													 m_DocumentWindows, m_ActiveScheme,
													 m_DiffWindows, m_Events));

		m_ClaudeSettings.Load();

		m_Panels.Add(Core::CreateScope<SessionsPanel>(
			m_Workspace, m_ClaudeSettings, m_SessionWindows,
			[this](const std::string& label, const ClaudeCommand& command,
				   const std::filesystem::path& workingDirectory) {
				return m_Terminals.OpenCommand(label, command.Program, command.Arguments, workingDirectory);
			}));

		m_Workspace.RegisterCommands(m_Commands);
		m_DocumentWindows.RegisterCommands(m_Commands, m_Keys);
		m_EditorSettings.RegisterCommands(m_Commands, m_Keys);
		explorer->RegisterCommands(m_Commands);
		m_Panels.RegisterCommands(m_Commands);
		m_Terminals.RegisterCommands(m_Commands, m_Keys);
		RegisterApplicationCommands();

		// After every default registered above, so an override shadows the binding it
		// replaces rather than being shadowed by it.
		m_AppSettings.ApplyKeyBindings(m_Keys);

		// Before any document opens: a choice is read at the first query for a language, and again after Reconfigure.
		m_LanguageServices.SetChoiceProvider([this](LanguageId language) { return ReadServerChoice(m_AppSettings, language); });
		m_LanguageServices.SetUninstallingQuery([this](std::string_view serverId) { return m_ServerInstalls.RunningJobOf(serverId) == InstallJobKind::Uninstall; });

		// Set before any document opens: the first
		// EnsureStarted is where a refused root must already be refused (ADR 0532).
		//
		// Every root is judged by itself: LanguageServices gives a file with no marker above it the
		// open folder or its own directory as a root, never an empty one.
		m_LanguageServices.SetRootPermission([this](const std::filesystem::path& root) { return m_Trust.IsTrusted(root); });

		// Before OpenStartupFolder, whose warm-up may start a server from Graphite's install.
		m_LanguageServices.SetServersDirectory(m_ServerInstalls.GetServersDirectory());
		m_LanguageServices.SetLogDirectory(UserDirectories::LogFile().parent_path());
		m_ServerInstalls.SweepAtStartup();

		const SettingsSources settingsSources{.Store = m_AppSettings,
											  .Editor = m_EditorSettings,
											  .Search = m_SearchSettings,
											  .Analysis = m_AnalysisSettings,
											  .Claude = m_ClaudeSettings,
											  .Scheme = m_ActiveScheme,
											  .Commands = m_Commands,
											  .Keys = m_Keys,
											  .Modals = m_Modals,
											  .Events = m_Events,
											  .Trust = m_Trust,
											  .Terminals = m_Terminals,
											  .Installs = m_ServerInstalls,
											  .Services = m_LanguageServices,
											  .SolutionLoaded = m_SolutionLoaded,
											  .ThemesDirectory = AppSettings::ThemesDirectory(),
											  .OnKeyBindingsChanged = [this] { m_AppSettings.SetKeyBindings(m_Keys); }};
		m_SettingsPanel = m_Panels.Add(Core::CreateScope<SettingsPanel>(BuildSettingsDirectory(settingsSources), m_ActiveScheme));

		ReportConflictingKeyBindings();
		SubscribeToWorkspaceEvents();

		// After every panel is registered and before the first frame draws any of them.
		const GlobalState stored = m_AppSettings.GetGlobalState();

		for (const auto& [id, visible] : stored.PanelVisibility)
		{
			m_Panels.SetVisible(id, visible);
		}

		m_StateKeeper.AdoptGlobal(stored);

		APP_INFO("Registered panels: {}", m_Panels.GetCount());

		explorer->LoadSettings();

		m_EditorSettings.Load();
		m_ActiveScheme.Load(m_AppSettings);
		m_SearchSettings.Load();

		// Beside the other settings, and before OpenStartupFolder below sets the root the first
		// frame will start a walk for: a pass begun under the default budget and only then
		// resized would evict its way down to the saved one for nothing.
		m_AnalysisSettings.Load();
		m_Analysis.SetBudget(m_AnalysisSettings.GetBudgetBytes());
		m_AnalysisCoordinator.GetPass().SetEnabled(m_AnalysisSettings.IsEnabled());

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

		// Enabled only where the toggle can say something true: with a folder open, and not when
		// an entry above the folder is what trusts it - revoking the folder itself would change
		// nothing, and the settings section is where that entry is.
		m_Commands.Add({.Id = "workspace.toggleTrust",
						.Label = "Trust This Folder",
						.Run =
							[this] {
								const std::filesystem::path& root = m_Workspace.GetRoot();

								if (m_Trust.IsTrusted(root))
								{
									m_Trust.Revoke(root);
								}
								else
								{
									m_Trust.Trust(root);
								}
							},
						.Enabled =
							[this] {
								const std::filesystem::path& root = m_Workspace.GetRoot();
								return m_Workspace.IsOpen() && (!m_Trust.IsTrusted(root) || m_Trust.IsEntry(root));
							},
						.Checked = [this] { return m_Workspace.IsOpen() && m_Trust.IsTrusted(m_Workspace.GetRoot()); }});

		// Through the window's close flag, not Application::Close(): the main loop only asks
		// a layer's OnCloseRequested when Window::ShouldClose() is true, and Close() alone
		// never sets it, so this is what puts Exit through the same question as Alt+F4.
		//
		// Alt+F4 is in the label rather than in the keymap: it is Windows' shortcut, and a
		// binding for it would advertise something that cannot be rebound.
		m_Commands.Add({.Id = "app.exit",
						.Label = "Exit\tAlt+F4",
						.Run = [] { Core::Application::Get().GetWindow().RequestClose(); }});

		m_Commands.Add({.Id = "help.about",
						.Label = "About Graphite",
						.Run = [this] { m_About.Open(); }});

		m_Commands.Add({.Id = "view.resetLayout",
						.Label = "Reset Layout",
						.Run = [this] {
							m_Panels.ResetVisibility();
							m_DockSpace.RequestLayoutReset();
							m_Terminals.RequestRedock();
							APP_INFO("Layout restored to the default");
						}});

		// Bound to nothing by default: there is no established chord for it yet, and a command
		// with no binding is still reachable through the command palette and the panels menu.
		m_Commands.Add({.Id = "search.focus",
						.Label = "Find in Files",
						.Run = [this] { m_SearchPanel->Focus(); }});

		// The active document, not the focused one: which editor holds the keyboard is only
		// knowable while drawing, and a command runs before any of them has drawn. Activating a
		// tab is what makes a document the one being worked in, so it is the same document
		// either way in every case a user can produce.
		m_Commands.Add({.Id = "find.open",
						.Label = "Find",
						.Run = [this] {
							if (const auto path = ActiveTextDocument(m_Documents))
							{
								if (const auto index = m_Documents.Find(*path))
								{
									m_DocumentWindows.OpenFind(m_Documents.At(*index));
								}
							} },
						.Enabled = [this] { return ActiveTextDocument(m_Documents).has_value(); }});

		m_Keys.SetDefault("find.open", {.Primary = Core::Chord::Key(ImGuiMod_Ctrl | ImGuiKey_F)});

		// The active document, for the same reason find.open above reads it rather than whatever
		// holds the keyboard: F12 runs before anything has drawn this frame, and activating a tab
		// is what makes it the document a command means either way.
		m_Commands.Add({.Id = kGoToDefinitionCommand,
						.Label = "Go to Definition",
						.Run = [this] {
							if (const auto path = ActiveTextDocument(m_Documents))
							{
								if (const auto index = m_Documents.Find(*path))
								{
									m_DocumentWindows.RequestDefinition(*index);
								}
							} },
						.Enabled = [this] {
							const auto path = ActiveTextDocument(m_Documents);
							return path.has_value() && m_LanguageServices.HasChosenServer(LanguageOfPath(*path)); }});

		m_Keys.SetDefault(kGoToDefinitionCommand, {.Primary = Core::Chord::Key(ImGuiKey_F12)});

		// The active document, for the same reason editor.goToDefinition reads it rather than
		// whatever holds the keyboard: Shift+F12 runs before anything has drawn this frame, and
		// activating a tab is what makes it the document a command means either way.
		m_Commands.Add({.Id = kFindReferencesCommand,
						.Label = "Find All References",
						.Run = [this] {
							if (const auto path = ActiveTextDocument(m_Documents))
							{
								if (const auto index = m_Documents.Find(*path))
								{
									m_DocumentWindows.RequestReferences(*index, *m_ReferencesPanel);
								}
							} },
						.Enabled = [this] {
							const auto path = ActiveTextDocument(m_Documents);
							return path.has_value() && m_LanguageServices.HasChosenServer(LanguageOfPath(*path)); }});

		m_Keys.SetDefault(kFindReferencesCommand,
						  {.Primary = Core::Chord::Key(ImGuiMod_Shift | ImGuiKey_F12)});

		m_Commands.Add({.Id = "languageServers.install",
						.Label = "Install Language Server",
						.Run = [this] {
							if (const std::optional<std::filesystem::path> path = ActiveTextDocument(m_Documents))
							{
								InstallChosenServerFor(LanguageOfPath(*path));
							} },
						.Enabled = [this] {
							const std::optional<std::filesystem::path> path = ActiveTextDocument(m_Documents);
							return path.has_value() && CanInstallChosenServerFor(*path); }});

		m_Commands.Add({.Id = "languageServers.settings",
						.Label = "Language Server Settings",
						.Run = [this] {
							const std::optional<std::filesystem::path> path = ActiveTextDocument(m_Documents);
							const LanguageId active = path ? LanguageOfPath(*path) : LanguageId::PlainText;
							OpenLanguageSettings(LanguageHasServers(active) ? active : LanguagesWithServers().front()); }});

#ifdef CORE_DEBUG
		m_Commands.Add({.Id = "debug.logTestEntries",
						.Label = "Log test entries",
						.Run = [] {
							APP_TRACE("Diagnostic entry");
							APP_INFO("Informational entry");
							APP_WARN("Warning entry");
							APP_ERROR("Sample error");
						}});

		for (const SampleToastCommand& sample : kSampleToastCommands)
		{
			m_Commands.Add({.Id = std::string(sample.CommandId),
							.Label = std::string(sample.Label),
							.ListLabel = std::string(sample.ListLabel),
							.Run = [this, sampleKind = sample.Kind] {
								m_Events.Publish(SampleToast(sampleKind, m_Workspace.GetRoot()));
							}});
		}
#endif
	}

	void GraphiteLayer::ReportConflictingKeyBindings()
	{
		// The defaults are assembled across five files, each of which believes it is claiming
		// a free chord. An error rather than an assertion: a duplicate binding is a wiring
		// mistake worth shouting about, not a reason to refuse to start.
		for (const Core::BindingConflict& conflict : m_Keys.FindConflicts())
		{
			APP_ERROR("'{}' and '{}' are both bound to {}", conflict.FirstCommandId, conflict.SecondCommandId,
					  conflict.SharedChord.ToString());
		}
	}

	void GraphiteLayer::SubscribeToWorkspaceEvents()
	{
		m_Git.SubscribeTo(m_Events);
		m_ChangeMarkers.SubscribeTo(m_Events);

		m_Events.Subscribe<WorkspaceOpenedEvent>([this](const WorkspaceOpenedEvent& event) {
			// First, before RestoreState below adds this folder's tabs: a tab opened with the
			// server resolves its root once and keeps it (server-roots spec), so the fallback must
			// already name this folder the first time any of them is resolved - including when the
			// folder is opened from the menu in the middle of a frame.
			m_LanguageServices.SetFallbackRoot(event.Root);

			// A folder dropped over another replaces it without a WorkspaceClosedEvent, so the
			// one being replaced is written here, before the new folder's tabs join the list.
			m_StateKeeper.CloseWorkspace();

			// Its documents stay open, but they are that folder's tabs, already written to it
			// just above, and recording them for this one would mix the two. At startup nothing
			// is open, so nothing is carried.
			m_DocumentWindows.CarryOverOpenDocuments();

			// Session windows follow the same rule: a session is not tied to a folder, so its
			// window stays open, but it was the replaced folder's to record.
			m_SessionWindows.CarryOverOpenSessions();
			m_Terminals.CarryOverOpenTerminals();

			// Asked once, here: trusting the folder later in the session reopens nothing and
			// starts nothing, because what restricted mode left closed stays the reader's to open.
			const bool trusted = m_Trust.IsTrusted(event.Root);

			// The keeper is told what is on disk, as it always is; the windows get only what
			// restricted mode lets them reopen (ADR 0532).
			const WorkspaceState stored = WorkspaceSettings{event.Root}.LoadState();
			const WorkspaceState restorable = RestorableState(stored, event.Root, trusted);
			m_DocumentWindows.RestoreState(restorable);
			m_SessionWindows.RestoreState(restorable.Sessions);
			m_Terminals.RestoreState(restorable.Terminals, trusted);
			m_StateKeeper.AdoptWorkspace(event.Root, stored);

			m_GitPort.SetRoot(event.Root);

			// A drop that replaces the workspace publishes no WorkspaceClosedEvent for the
			// folder it replaces - GitModel.cpp documents the same trap and clears on both
			// events for the same reason. Without this, a diff tab opened against the old
			// repository survives into the new one, showing "no longer loaded" and offering
			// a Reload that would ask the new repository for a commit it never had.
			m_DiffWindows.CloseAll();

			// After CloseAll, never before: the diffs this folder had open last time are the
			// only ones that belong to it.
			m_DiffWindows.RestoreState(restorable.Diffs);

			// Same trap, same reason: an expectation registered against the folder this drop
			// replaced has no business claiming a path in the one that just opened. Dropping a
			// new folder onto the window is a folder close as far as the user is concerned, even
			// though no WorkspaceClosedEvent says so.
			m_DocumentWindows.ClearExpectedWrites();
		});

		// A failure reported for the folder being replaced - by a drop too, which publishes no
		// WorkspaceClosedEvent - is about a folder no longer open; an error toast would otherwise
		// linger for its twelve seconds over the new one. Here and not on WorkspaceOpenedEvent:
		// by then the new folder has reported its own failures, and clearing would take those
		// with it. Only the folder's: a toast about the application still applies.
		m_Events.Subscribe<WorkspaceOpeningEvent>([this](const WorkspaceOpeningEvent&) {
			m_Toasts.Clear(ToastGroupOf(ToastScope::Folder));
		});

		m_Events.Subscribe<OpenFileRequested>([this](const OpenFileRequested& event) {
			m_DocumentWindows.RequestOpen(event.Path, event.Permanent);
		});

		// Delivered synchronously, from the Explorer's menu, which is drawn by m_Panels before
		// m_Terminals.Render in OnImGuiRender - so the new entry is never pushed inside the loop
		// over the open terminals that OpenShellIn must not be called from.
		m_Events.Subscribe<OpenTerminalRequested>([this](const OpenTerminalRequested& event) {
			static_cast<void>(m_Terminals.OpenDefaultShellIn(event.Directory)); // reports its own failure
		});

		// Any panel can speak through the status bar by publishing this; the editor's own
		// transient slot is where the bar reads it from.
		m_Events.Subscribe<StatusMessageEvent>([this](const StatusMessageEvent& event) {
			m_DocumentWindows.SetTransientMessage(event.Text);
		});

		// And raise a toast the same way, so no feature holds the toasts themselves. The scope
		// becomes the toast's group, which the folder handlers above and below clear by.
		m_Events.Subscribe<ToastEvent>([this](const ToastEvent& event) {
			ShowToast(event.Kind, event.Title, event.Message, event.CopyText, event.Scope);
		});

		m_Events.Subscribe<WorkspaceClosedEvent>([this](const WorkspaceClosedEvent&) {
			// No folder is open, so a file with no marker is rooted at its own directory again.
			m_LanguageServices.SetFallbackRoot({});

			// From the last frame's snapshot, so it does not matter that the documents are
			// closed just below.
			m_StateKeeper.CloseWorkspace();

			// The shells stay open, as they always have, but they were this folder's: a folder
			// opened next must not save them as its own.
			m_Terminals.CarryOverOpenTerminals();

			m_Documents.CloseAll();

			// Without this, GitModel stops asking - it handles this same event - but the
			// port's cached repository handles outlive the folder they were opened on,
			// which is exactly what blocks the user's own deletion of .git on Windows.
			m_GitPort.SetRoot({});

			// A diff of a commit in a folder that is no longer open is a tab about nothing.
			m_DiffWindows.CloseAll();

			// An expectation registered against one repository has no business claiming a
			// path in whatever folder opens next. DocumentWindows has no closed-handler of
			// its own to put this in - this event handler is where the rest of the per-folder
			// state above is already dropped, so the expectations join it here.
			m_DocumentWindows.ClearExpectedWrites();

			// An error toast stays up for twelve seconds, so one about a git write in this folder
			// would outlive it. GitModel drops the failures nobody took yet on the same event.
			// Only the folder's toasts go; one about the application outlives the folder.
			m_Toasts.Clear(ToastGroupOf(ToastScope::Folder));
		});

		m_Events.Subscribe<WorkspaceRootMovedEvent>([this](const WorkspaceRootMovedEvent& event) {
			m_DocumentWindows.HandleRootMoved(event.From, event.To);
			m_StateKeeper.MoveWorkspace(event.To);
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
		{
			startFolder = m_AppSettings.GetLastFolder();
		}

		if (!startFolder.empty())
		{
			// OpenFolder reports its own failure, and starting with no folder open is the fallback.
			static_cast<void>(m_Workspace.OpenFolder(startFolder));
		}

		// No folder to restore terminals for: one default shell, as the terminal always opened.
		if (!m_Workspace.IsOpen())
		{
			static_cast<void>(m_Terminals.OpenStartupShell()); // reports its own failure
		}
	}

	void GraphiteLayer::OnUpdate(Core::Timestep ts)
	{
		m_Workspace.Update();

		// Before the panels, so the model applies whatever a worker finished this frame -
		// and picks up any workspace event just published - before any panel reads a
		// snapshot this frame.
		m_Git.OnUpdate(ts);

		// A stage, discard or commit from this process reaches the gutter here, not only
		// through the watcher, which never sees `.git/` when the workspace is a subfolder of the
		// repository or a linked worktree.
		if (const std::uint64_t writes = m_Git.GetAppliedWriteCount(); writes != m_SeenGitWrites)
		{
			m_SeenGitWrites = writes;
			m_ChangeMarkers.Invalidate();
		}

		m_ChangeMarkers.OnUpdate(ts);

		m_Panels.OnUpdate(ts);
		m_Terminals.Update();

		m_SolutionLoaded = m_Workspace.GetLoadedSolution().has_value();
		// In OnUpdate, ahead of the Poll in OnImGuiRender, so a server started this frame starts on this selection.
		m_LanguageServices.SetSelectedSolution(m_Workspace.GetSelectedSolution());

		UpdateFocusReload(ts);

		m_DocumentWindows.TickExpectedWrites(ts);

		// Watchers drop events — buffer overflow, or a tool touching many files at once — and
		// returning from another application is exactly when that matters.
		m_DocumentWindows.NotifyWindowFocus(MainWindowHasFocus());

		m_StateKeeper.Observe(ts.GetSeconds(), CaptureGlobalState(),
							  m_Workspace.IsOpen() ? std::optional<WorkspaceState>{CaptureWorkspaceState()}
												   : std::nullopt);
	}

	void GraphiteLayer::UpdateFocusReload(Core::Timestep ts)
	{
		// The process, not the main window: a panel dragged out into its own viewport is a
		// separate OS window, and working in one is not having left the application.
		if (!Core::IsProcessInForeground())
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

	GlobalState GraphiteLayer::CaptureGlobalState()
	{
		GlobalState state;
		state.Window = Core::Application::Get().GetWindow().GetPlacement();

		m_Panels.ForEach([&state](Core::Panel& panel) {
			if (panel.IsVisible() != panel.IsDefaultVisible())
			{
				state.PanelVisibility[panel.GetId()] = panel.IsVisible();
			}
		});

		return state;
	}

	WorkspaceState GraphiteLayer::CaptureWorkspaceState() const
	{
		WorkspaceState state = m_DocumentWindows.CaptureState();
		state.Sessions = m_SessionWindows.CaptureState();
		state.Diffs = m_DiffWindows.CaptureState();
		state.Terminals = m_Terminals.CaptureState();

		return state;
	}

	void GraphiteLayer::OnDetach()
	{
		m_StateKeeper.Flush();

		// After StateKeeper, whose flush hands the window state to the settings.
		m_AppSettings.Flush();
	}

	Core::Ref<Core::HttpClient> GraphiteLayer::CreateInstallClient()
	{
		Core::Result<Core::Scope<Core::HttpClient>> created = Core::CreateHttpClient(std::string{"Graphite/"} + Version::String());

		if (!created)
		{
			APP_WARN("Language servers cannot be downloaded: {}", created.GetError().Message);
			return nullptr;
		}

		return Core::Ref<Core::HttpClient>{std::move(created.GetValue())};
	}

	void GraphiteLayer::ApplyFinishedInstalls()
	{
		m_ServerInstalls.Poll();

		std::vector<InstallProgress> finished;
		m_ServerInstalls.TakeFinished(finished);

		for (const InstallProgress& outcome : finished)
		{
			RestartServersAfter(outcome, m_LanguageServices);

			switch (outcome.Step)
			{
			case InstallStep::Installed:
				m_Events.Publish(ApplicationToast(Core::ToastKind::Success, "Language server installed",
												  std::format("{} {} installed", ServerDisplayName(outcome.ServerId), outcome.Version)));
				break;
			case InstallStep::Failed:
				m_Events.Publish(ApplicationToast(Core::ToastKind::Error, ServerDisplayName(outcome.ServerId), outcome.Failure));
				break;
			case InstallStep::Resolving:
			case InstallStep::Downloading:
			case InstallStep::Verifying:
			case InstallStep::Extracting:
			case InstallStep::UpToDate:
			case InstallStep::UpdateAvailable:
			case InstallStep::Uninstalled:
			case InstallStep::Cancelled:
				break;
			}
		}
	}

	bool GraphiteLayer::CanInstallChosenServerFor(const std::filesystem::path& path)
	{
		const ServerDefinition* server = m_LanguageServices.ChosenServer(LanguageOfPath(path));

		return server != nullptr && MayInstall(m_ServerInstalls, *server);
	}

	void GraphiteLayer::InstallChosenServerFor(LanguageId language)
	{
		if (const ServerDefinition* server = m_LanguageServices.ChosenServer(language))
		{
			m_ServerInstalls.Install(server->Id);
		}
	}

	void GraphiteLayer::PublishServerMessages()
	{
		std::vector<ServerMessage> messages;
		m_LanguageServices.TakeMessages(messages);

		for (ServerMessage& message : messages)
		{
			m_Events.Publish(ApplicationToast(ToastKindOf(message.Kind), std::move(message.Server), std::move(message.Text)));
		}
	}

	// Shows a panel that was hidden and brings it to the front of its dock node. The focus
	// is applied on the panel's next Begin, which is next frame for a panel just made visible.
	void GraphiteLayer::RevealPanel(std::string_view id)
	{
		Core::Panel* panel = m_Panels.Find(id);

		if (panel == nullptr)
		{
			return;
		}

		m_Panels.SetVisible(id, true);
		panel->RequestFocus();
		Core::Redraw::Request();
	}

	void GraphiteLayer::OpenLanguageSettings(LanguageId language)
	{
		m_SettingsPanel->Open({"languageServers", std::string{DefinitionOf(language).SettingsName}});
		RevealPanel("settings");
	}

	void GraphiteLayer::ApplyChromeIfSchemeChanged()
	{
		if (m_AppliedSchemeVersion == m_ActiveScheme.GetVersion())
		{
			return;
		}

		m_AppliedSchemeVersion = m_ActiveScheme.GetVersion();

		const Core::ChromePalette palette = ChromePaletteFor(m_ActiveScheme.Get());

		Core::ImGuiRenderer::ApplyTheme(palette);
		Core::Application::Get().GetWindow().SetBorderColor(palette.Border);
	}

	void GraphiteLayer::UpdateGitDecorations()
	{
		// Compared by pointer: GitModel hands out a new snapshot for every status it applies, so an
		// unchanged pointer is an unchanged status and the index built from it still holds.
		if (Core::Ref<const Core::WorkingTreeStatus> status = m_Git.GetStatus(); status != m_DecoratedStatus)
		{
			m_Decorations = status ? std::make_unique<GitDecorations>(*status) : nullptr;
			++m_DecorationsVersion;
			m_DecoratedStatus = std::move(status);
		}

		GitDecorationView view;

		// No folder, or a folder that is not a repository (no status ever arrives): nothing is
		// decorated, and the Explorer looks as it does without git.
		if (m_Workspace.IsOpen() && m_Decorations)
		{
			const GitPalette palette = ReadGitPalette(m_ActiveScheme.Get());

			view.Index = m_Decorations.get();
			view.Version = m_DecorationsVersion;
			view.SetWorkingDirectory(m_Git.GetWorkingDirectory());
			view.Colours[0] = palette.GutterAdded;
			view.Colours[1] = palette.GutterModified;
			view.Colours[2] = palette.GutterRemoved;
		}

		m_Explorer->SetGitDecorations(view);
		m_DocumentWindows.SetGitDecorations(view);
	}

	void GraphiteLayer::OnImGuiRender()
	{
		// First statement, before any window is begun: ImGuiStyle::Colors must not change
		// between a Begin and its End.
		ApplyChromeIfSchemeChanged();

		StatusMessages messages{m_DocumentWindows.GetStatusMessage(),
								m_SearchPanel->GetStatusMessage()};

		// Read once and spent everywhere below - the analysis's switches, the status bar's server
		// slot and the restricted-mode bar. Separate reads of the same getter are chances for those
		// to disagree, and a bar saying "finding files" for a walk that was switched off is
		// exactly what disagreement looks like. Includes whether a folder is open: an empty root is
		// never trusted.
		const bool workspaceTrusted = m_Workspace.IsOpen() && m_Trust.IsTrusted(m_Workspace.GetRoot());

		// The folder, its trust and the setting, compared against last frame's: warms the servers,
		// revalidates them when the trust list moves, and starts, restarts or stops the walk and
		// the store. Before Poll, so a server this starts is drained on this same frame.
		m_AnalysisCoordinator.Update(AnalysisSwitches{.Root = m_Workspace.GetRoot(),
													  .SelectedSolution = m_Workspace.GetSelectedSolution(),
													  .Trusted = workspaceTrusted,
													  .SettingEnabled = m_AnalysisSettings.IsEnabled(),
													  .TrustRevision = m_Trust.GetRevision()});

		// Before anything reads a status, not after. Poll is what turns a warm-up's findings into
		// started servers and what moves a running one from Starting to Indexing to Ready — so a
		// status read ahead of it is always one frame stale, and on the frame a server first
		// appears there is nothing to read at all.
		m_LanguageServices.Poll();
		PublishServerMessages();
		ApplyFinishedInstalls();

		// Before anything draws, for the reason the Tick below gives: the panels render at 512
		// and DocumentWindows drains its own answers at 516, so a model fed any later is read by
		// the panel one frame before it is written - and on every frame a diagnostic arrives,
		// the squiggle and the panel row disagree.
		DiagnosticsUpdate diagnostics;
		m_LanguageServices.TakeDiagnostics(diagnostics);
		m_Diagnostics.Apply(diagnostics);

		// The same "is this document open" predicate ProblemsPanel filters its own rows by - see
		// DiagnosticsModel::Total's own comment for why.
		const auto isOpenDocument = [this](const std::filesystem::path& path) { return m_Documents.Find(path).has_value(); };

		// Preformatted here rather than handed to StatusBar.cpp as a Counts, the same way Server
		// is preformatted from a ServerStatus below: it keeps DiagnosticsModel out of a file whose
		// only other dependency is Core::DocumentSet.
		const DiagnosticsModel::Counts counts = m_Diagnostics.Total(isOpenDocument);
		messages.Problems = ProblemCountText(counts.Errors, counts.Warnings);

		// Before the panels draw, so what they declare this frame is stamped with this frame's
		// number, and so an entry nothing declared last frame is closed before anything asks
		// about it again.
		m_PreviewSemantics.Tick(static_cast<std::size_t>(ImGui::GetFrameCount()),
								ImGui::GetTime());

		// The open document's own questions come first. IsAnalysing is what the Server slot below
		// already asks to decide whether to say "analysing" rather than "ready", which makes it
		// exactly the condition worth yielding to.
		const std::optional<std::filesystem::path> activeDocument = ActiveTextDocument(m_Documents);
		const bool foregroundBusy = activeDocument && m_DocumentWindows.IsAnalysing(*activeDocument);

		// After Poll and after PreviewSemantics::Tick: the pass asks whether a server is ready and
		// whether a preview pane holds a document, and both are settled for this frame only now.
		//
		// The budget and the concurrency are read every frame rather than on a change
		// notification: both are a comparison against a value that almost never differs, and a
		// settings object with no observers is one less thing to keep in step.
		//
		// "Somebody else already holds this document with the server", not "a tab does".
		// LanguageServices::OpenDocuments counts no references, so whichever of the three openers
		// sends a didClose takes the file away from all of them - and a preview pane holds no
		// DocumentSet entry, so a tab check alone would let the background pass close a document a
		// pane is still asking about. See ADR 0512.
		m_AnalysisCoordinator.Tick(AnalysisTickInputs{
			.Now = ImGui::GetTime(),
			.ForegroundBusy = foregroundBusy,
			.BudgetBytes = m_AnalysisSettings.GetBudgetBytes(),
			.Concurrency = m_AnalysisSettings.GetConcurrency(),
			.TabWidth = m_EditorSettings.GetIndentStyle().Width,
			.HeldElsewhere = [this](const std::filesystem::path& path) { return m_Documents.Find(path).has_value() || m_PreviewSemantics.Holds(path); }});

		// EnsureStarted is safe to call every frame the document stays active — see its own
		// comment; the common case is a single map lookup.
		if (activeDocument)
		{
			m_LanguageServices.EnsureStarted(*activeDocument, LanguageOfPath(*activeDocument));
		}

		// What the bar describes is the document on screen, not the active one: the active
		// document outlives its tab being covered by another, and the bar went on naming it.
		const std::optional<std::size_t> shownIndex = ShownActiveDocument(
			m_Documents, [this](const std::filesystem::path& path) { return m_DocumentWindows.IsShown(path); });
		const std::optional<std::filesystem::path> shownDocument =
			shownIndex ? std::optional{m_Documents.At(*shownIndex).Path} : std::nullopt;

		ServerStatus status;
		LanguageId activeLanguage = LanguageId::PlainText;

		if (shownDocument)
		{
			activeLanguage = LanguageOfPath(*shownDocument);
			status = m_LanguageServices.StatusFor(*shownDocument, activeLanguage);
		}

		std::optional<LanguagePrompt> languagePrompt;

		if (shownDocument)
		{
			const ServerChoice& choice = m_LanguageServices.ChoiceFor(activeLanguage);

			// Without the overrides: the bar needs only which server, and they hold JSON that would be copied every frame.
			languagePrompt = LanguagePromptFor(LanguagePromptInputs{
				.Language = activeLanguage,
				.Choice = ServerChoice{.Kind = choice.Kind, .Server = choice.Server},
				.State = status.State,
				.Install = choice.Server != nullptr ? InstallInputsFor(m_ServerInstalls, *choice.Server) : InstallInputs{},
				.Dismissed = m_DismissedLanguagePrompts.contains(activeLanguage),
				.RestrictedModeShown = m_Workspace.IsOpen() && !workspaceTrusted});
		}

		ServerLineInputs serverInputs{.Active = status,
									  .ActiveLanguage = activeLanguage,
									  .FolderOpen = m_Workspace.IsOpen(),
									  .FolderTrusted = workspaceTrusted,
									  .Overall = m_LanguageServices.OverallStatus()};

		if (shownDocument && m_DocumentWindows.IsAnalysing(*shownDocument))
		{
			serverInputs.AnalysingFile = Core::ToUtf8(shownDocument->filename());
		}

		if (const ServerDefinition* chosen = m_LanguageServices.ChosenServer(activeLanguage); shownDocument && chosen != nullptr)
		{
			serverInputs.ActiveJob = ServerJobFor(m_ServerInstalls, *chosen);
		}

		std::optional<LanguageId> serverSettingsLanguage;

		if (std::optional<ServerLine> line = ChooseServerLine(serverInputs))
		{
			messages.Server = std::move(line->Text);
			messages.ServerDetail = std::move(line->Detail);
			messages.ServerOpensSettings = line->SettingsLanguage.has_value();
			serverSettingsLanguage = line->SettingsLanguage;
		}

		// Its own slot beside the server's, because it answers a different question: the server
		// phrase is about the file on screen, this one is about the tree behind it. Empty while
		// there is nothing to say, which is what leaves the slot unoccupied rather than resting on
		// a finished count nobody is waiting for.
		if (std::string background = BackgroundAnalysisText(m_AnalysisCoordinator.GetPass().Progress());
			!background.empty())
		{
			messages.Background = std::move(background);
			messages.BackgroundDetail = BackgroundAnalysisDetail(
				m_AnalysisCoordinator.GetPass().Progress(), m_Analysis.GetBytes(), m_Analysis.GetBudget());
		}

		{
			const auto head = m_Git.GetHead();
			const auto branches = m_Git.GetBranches();
			const auto gitStatus = m_Git.GetStatus();

			messages.Git = GitStatusText({
				.FolderOpen = m_Workspace.IsOpen(),
				.MissingRepository = IsMissingRepository(gitStatus != nullptr, m_Git.GetStatusError()),
				.Head = head.get(),
				.Branches = branches.get(),
				.Status = gitStatus.get(),
			});
		}

		// Drained here rather than after the toasts draw, so a toast cleared with its folder since
		// the last frame already counts for the bell this frame.
		for (const Core::ToastDeparture& departure : m_Toasts.TakeDepartures())
		{
			m_Notifications.NoteDeparture(departure);
		}

		// Not while the popup is open: it marks everything seen when it draws, after the bar, and
		// the dot would flash for the frame between.
		const std::optional<Core::ToastKind> missedKind = m_Notifications.MostSevereMissed();
		const bool missedOutsideTheOpenList = missedKind.has_value() && !m_NotificationsPopup.IsOpen();
		if (missedOutsideTheOpenList)
		{
			messages.MissedNotificationColour = Core::ToastAccentOf(ToastPaletteFor(m_ActiveScheme.Get()), *missedKind);
		}

		switch (DrawStatusBar(m_Documents, shownIndex, m_Workspace.GetRoot(), messages))
		{
		case StatusBarClick::GitBranch:
			RevealPanel("git");
			break;

		case StatusBarClick::GitChanges:
			RevealPanel("changes");
			break;

		case StatusBarClick::Server:
			if (serverSettingsLanguage)
			{
				OpenLanguageSettings(*serverSettingsLanguage);
			}
			break;

		case StatusBarClick::Notifications:
			m_NotificationsPopup.OpenFromBell(kStatusBarWindowName);
			break;

		case StatusBarClick::None:
			break;
		}

		// Every frame rather than once: the caption follows the interface scale, which the
		// monitor the window is on or the settings can change at any moment.
		m_Layout.MenuBarHeight = m_CustomTitleBar ? Core::Scaled(kCaptionHeight) : 0.0f;
		m_DockSpace.Begin(m_Layout, [this, workspaceTrusted, &languagePrompt] {
			RenderRestrictedModeBar(workspaceTrusted);

			if (languagePrompt)
			{
				RenderLanguageSupportBar(*languagePrompt);
			}
		});
		m_MenuBar.Render(m_Commands, m_Keys, m_MenuTree, m_Layout.MenuBarHeight, [this] { m_TitleBar.RenderLogo(Core::Application::Get().GetWindow()); }, [this] { m_TitleBar.Render(Core::Application::Get().GetWindow()); });
		RenderEmptyEditorArea();
		m_DockSpace.End();

		// Outside the dockspace host's Begin/End above, and outside every other window: a
		// modal popup must not be opened from inside a window that may not be current when
		// the popup needs to draw.
		RenderCloseConfirm();
		RenderDropConfirm();
		m_About.Render();
		RenderTrustParentConfirm();

		UpdateGitDecorations();

		// Panels render outside the host window — inside it they would dock as ordinary child
		// windows instead of landing in the dockspace nodes.
		m_Panels.OnImGuiRender(m_TabCloseRequests);

		const CommandContext commandContext{.Commands = &m_Commands,
											.Keys = &m_Keys,
											.Router = &m_Router,
											.Input = &m_FrameInput,
											.ContextMenu = &m_EditorContextMenu};

		m_DocumentWindows.Render(commandContext,
								 EditorFrame{.FontSize = m_EditorSettings.GetFontSize(),
											 .Indent = m_EditorSettings.GetIndentStyle(),
											 .FallbackDockNode = m_DockSpace.GetCenterNodeId(),
											 .WordWrap = m_EditorSettings.IsWordWrap(),
											 .ChangeMarkers = {.Gutter = m_EditorSettings.HasGutterChangeMarkers(),
															   .Scrollbar = m_EditorSettings.HasScrollbarChangeMarkers()},
											 .Hover = m_EditorSettings.GetHoverSettings(),
											 .Scheme = m_ActiveScheme.Get(),
											 .CloseRequests = m_TabCloseRequests,
											 .WorkspaceRoot = m_Workspace.GetRoot()},
								 *m_ReferencesPanel);

		m_DiffWindows.Render(m_Git, ReadGitPalette(m_ActiveScheme.Get()),
							 ReadSyntaxPalette(m_ActiveScheme.Get()),
							 ReadEditorPalette(m_ActiveScheme.Get()).Selection,
							 m_EditorSettings.IsDiffWordWrap(), m_DockSpace.GetCenterNodeId(),
							 m_TabCloseRequests);

		m_SessionWindows.Render(m_DockSpace.GetCenterNodeId(), ReadSessionPalette(m_ActiveScheme.Get()),
								m_TabCloseRequests);

		const Core::Panel* console = m_Panels.Find("console");

		std::vector<std::string> rowWindows;

		for (const std::string& id : m_Layout.Bottom)
		{
			if (const Core::Panel* panel = m_Panels.Find(id); panel != nullptr)
			{
				rowWindows.push_back(panel->GetWindowName());
			}
		}

		m_Terminals.Render(TerminalDockTargets{.BesideWindow = console != nullptr ? console->GetWindowName() : std::string{},
											   .CentreNode = m_DockSpace.GetCenterNodeId(),
											   .RowWindows = std::move(rowWindows),
											   .DockSpace = m_DockSpace.GetDockSpaceId(),
											   .BottomRatio = m_Layout.BottomRatio,
											   .LayoutRebuilt = m_DockSpace.HasBuiltLayoutThisFrame()},
						   ReadEditorPalette(m_ActiveScheme.Get()), m_TabCloseRequests);

		// Drained here, not in a git panel, so a failure is reported with both panels closed
		// and only once with both open.
		while (std::optional<GitModel::WriteFailure> failure = m_Git.TakeWriteError())
		{
			// Paths inside the repository read relative to it on screen; Copy keeps them whole.
			std::string title(WriteFailureTitle(failure->Kind));
			std::string shown = RelativiseUnder(failure->Error.Message, m_Git.GetWorkingDirectory());
			std::string copied = title + "\n" + failure->Error.Message;

			// About the open repository, so the folder's close or replacement clears it.
			ShowToast(Core::ToastKind::Error, std::move(title), std::move(shown), std::move(copied), ToastScope::Folder);
		}

		// Last of what draws, so the toasts sit over every panel and terminal.
		const Core::ToastPalette toastPalette = ToastPaletteFor(m_ActiveScheme.Get());
		m_NotificationsPopup.Render(m_Notifications, toastPalette, m_Workspace.GetRoot(), m_LocalTimeZone);
		m_Toasts.Render(toastPalette);

		// After every owner has drawn: what nobody took this frame belonged to a window that
		// went some other way, and what the menus asked this frame becomes due for the next.
		m_TabCloseRequests.EndFrame();

		// The last point in the frame that reads the queue: what a frame drains is exactly
		// what arrived since the previous one was drawn.
		m_FrameInput.Clear();

		// Last, after everything that can change a setting this frame - the panels and the commands
		// they ran here, StateKeeper in OnUpdate - so one frame's changes make one write.
		m_AppSettings.Flush();
	}

	void GraphiteLayer::OnEvent(Core::Event& event)
	{
		// Before the panels: a drop is not input anybody is competing for, and routing it
		// through the shortcut path would only give the panels a chance to swallow it.
		HandleFileDrop(event);

		if (event.Handled)
		{
			return;
		}

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

	void GraphiteLayer::HandleFileDrop(Core::Event& event)
	{
		Core::EventDispatcher dispatcher{event};

		dispatcher.Dispatch<Core::FileDropEvent>([this](Core::FileDropEvent& dropped) {
			const DropRouting routing =
				ClassifyDrop(dropped.GetPaths(), [](const std::filesystem::path& path) {
					std::error_code code;
					return std::filesystem::is_directory(path, code);
				});

			// The workspace before the files, so a file dropped along with the folder it
			// lives in opens into that folder rather than the one being replaced. Waiting
			// for an answer is the exception: the question is not worth holding the files
			// hostage, and opening a folder leaves open documents where they are.
			if (routing.Target != DropTarget::None)
			{
				const DroppedWorkspace workspace{routing.Target, routing.Root};

				if (ReplacesWorkspace(routing, m_Workspace.GetRoot()))
				{
					m_DropQuestion.Ask(workspace);
				}
				else
				{
					OpenDropped(workspace);
				}
			}

			// Permanently, not as a preview: dragging a file onto the window is as deliberate
			// as double-clicking one in the tree.
			for (const std::filesystem::path& file : routing.Files)
			{
				m_Workspace.RequestOpenFile(file, true);
			}

			return true;
		});
	}

	void GraphiteLayer::OpenDropped(const DroppedWorkspace& dropped)
	{
		switch (dropped.Target)
		{
		case DropTarget::None:
			break;

		case DropTarget::Folder:
			// OpenFolder reports its own failures, the way the folder picker's caller leaves
			// them to it.
			static_cast<void>(m_Workspace.OpenFolder(dropped.Root));
			break;

		case DropTarget::Solution:
			// Logged only: OpenSolution reports every failure in a toast itself.
			if (const auto opened = m_Workspace.OpenSolution(dropped.Root); !opened)
			{
				APP_ERROR("Cannot open the solution: {}", opened.GetError().Message);
			}
			break;
		}
	}

	void GraphiteLayer::ShowToast(Core::ToastKind kind, std::string title, std::string message, std::string copyText,
								  ToastScope scope)
	{
		const std::uint64_t toastId = m_Toasts.Show(kind, title, message, copyText, ToastGroupOf(scope));
		const bool aboutTheFolder = scope == ToastScope::Folder;

		m_Notifications.Record({
			.ToastId = toastId,
			.Kind = kind,
			.Title = std::move(title),
			.Message = std::move(message),
			.CopyText = std::move(copyText),
			.Folder = aboutTheFolder ? m_Workspace.GetRoot() : std::filesystem::path(),
			.LastShownAt = std::chrono::system_clock::now(),
		});
	}

	void GraphiteLayer::RenderEmptyEditorArea()
	{
		// The explorer beside it already says no folder is open, so this says what can be done
		// instead. The button stays: the explorer can be closed, and then this is the only way
		// in on screen. A file dropped without a folder still opens as a document.
		if (!m_Workspace.IsOpen())
		{
			// Greyed out while a dialog is open, as the explorer's button is.
			ImGui::BeginDisabled(m_Workspace.IsDialogOpen());
			const bool clicked =
				m_DockSpace.CenterEmptyState("Open a folder to start working, or drop a file here.", "Open Folder");
			ImGui::EndDisabled();

			if (clicked)
			{
				OpenFolderInteractively(m_Workspace);
			}

			return;
		}

		m_DockSpace.CenterEmptyState("Open a file from the Explorer, or drop one here.");
	}

	void GraphiteLayer::RenderLanguageSupportBar(const LanguagePrompt& prompt)
	{
		switch (DrawLanguageSupportBar(prompt))
		{
		case LanguageBarChoice::None:
			break;

		case LanguageBarChoice::Configure:
		case LanguageBarChoice::OpenSettings:
			OpenLanguageSettings(prompt.Language);
			break;

		case LanguageBarChoice::Install:
			m_ServerInstalls.Install(prompt.ServerId);
			break;

		case LanguageBarChoice::Cancel:
			m_ServerInstalls.Cancel(prompt.ServerId);
			break;

		case LanguageBarChoice::GetRuntime:
			if (const Core::Result<void> opened = Core::Shell::OpenInBrowser(DotnetDownloadUrl(prompt.RuntimeMajor)); !opened)
			{
				m_Events.Publish(ApplicationToast(Core::ToastKind::Error, "Cannot open the browser", opened.GetError().Message));
			}
			break;

		case LanguageBarChoice::NotNow:
			m_DismissedLanguagePrompts.insert(prompt.Language);
			break;
		}
	}

	void GraphiteLayer::RenderRestrictedModeBar(bool trusted)
	{
		if (!m_Workspace.IsOpen() || trusted)
		{
			return;
		}

		// Copied: trusting writes the list, and nothing here should hold a reference into state
		// that the click is about to change.
		const std::filesystem::path root = m_Workspace.GetRoot();
		const std::optional<std::filesystem::path> parent = ParentWorthTrusting(root);

		switch (DrawRestrictedModeBar(parent))
		{
		case RestrictedBarChoice::None:
			break;

		case RestrictedBarChoice::TrustFolder:
			m_Trust.Trust(root);
			break;

		case RestrictedBarChoice::TrustParent:
			// Asked first: this trusts every folder under the parent, not just this one.
			m_TrustParentQuestion.Ask(*parent);
			break;
		}
	}

	void GraphiteLayer::RenderTrustParentConfirm()
	{
		// Fixed width, so a long parent path wraps rather than widening the dialog.
		DialogLayout::BeforeBegin(DialogLayout::kNarrowWidth);

		// A question still queued when its folder closed, was replaced, or got trusted some other
		// way is dropped rather than asked about a parent that no longer has anything to do with
		// what is open.
		const bool open = m_TrustParentQuestion.Begin([this](const std::filesystem::path& parent) {
			if (!m_Workspace.IsOpen())
			{
				return false;
			}

			const std::filesystem::path root = m_Workspace.GetRoot();
			return !m_Trust.IsTrusted(root) && ParentWorthTrusting(root) == parent;
		});

		DialogLayout::AfterBegin();

		if (!open)
		{
			return;
		}

		const std::filesystem::path* asked = m_TrustParentQuestion.GetCurrent();

		if (asked == nullptr)
		{
			ImGui::CloseCurrentPopup();
			m_TrustParentQuestion.End();
			return;
		}

		// Copied out: Answer clears what GetCurrent points at, and trusting writes the list.
		const std::filesystem::path parent = *asked;

		DialogLayout::Text(TrustAllMessage(parent));
		DialogLayout::Gap();

		// Trust All is the dangerous choice, so it is the destructive role: Enter does nothing,
		// and Escape cancels as the Cancel button does.
		const DialogLayout::Choice choice = DialogLayout::ButtonRow(
			{}, {.Label = "Trust All"},
			ImGui::ColorConvertU32ToFloat4(ReadEditorPalette(m_ActiveScheme.Get()).DiagnosticError));

		if (choice == DialogLayout::Choice::Destructive)
		{
			m_Trust.Trust(parent);
			m_TrustParentQuestion.Answer();
		}
		else if (choice == DialogLayout::Choice::Cancel)
		{
			m_TrustParentQuestion.Answer();
		}

		m_TrustParentQuestion.End();
	}

	void GraphiteLayer::RenderDropConfirm()
	{
		if (!m_DropQuestion.Begin())
		{
			return;
		}

		// Copied out before anything is opened: Answer clears what GetCurrent points at, and
		// opening a workspace is not something to do with a dangling pointer to its path.
		const DroppedWorkspace* asked = m_DropQuestion.GetCurrent();
		const DroppedWorkspace dropped = asked != nullptr ? *asked : DroppedWorkspace{};

		ImGui::TextUnformatted("Open this in place of the workspace now open?");
		ImGui::Separator();
		ImGui::TextUnformatted(Core::ToUtf8(dropped.Root).c_str());
		ImGui::Separator();

		if (ImGui::Button("Open"))
		{
			OpenDropped(dropped);
			m_DropQuestion.Answer();
		}

		ImGui::SameLine();

		if (ImGui::Button("Cancel"))
		{
			m_DropQuestion.Answer();
		}

		m_DropQuestion.End();
	}

	void GraphiteLayer::RouteShortcuts(Core::Event& event)
	{
		Core::EventDispatcher dispatcher{event};

		dispatcher.Dispatch<Core::KeyPressedEvent>([this](Core::KeyPressedEvent& pressed) {
			return m_Router.OnKeyPressed(m_Commands, m_Keys, Core::ShortcutScope::Global, pressed);
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
		{
			return true;
		}

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
			{
				ImGui::BulletText("%s", Core::ToUtf8(item.Path.filename()).c_str());
			}
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