#include "App/GraphiteLayer.h"

#include "Core/Base/Base.h"

#include "Editor/ActiveTextDocument.h"
#include "Editor/EditorCommands.h"
#include "Theme/Palettes.h"
#include "UI/ClipboardCopy.h"
#include "Editor/StatusBar/ServerReportText.h"
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
#include "Git/GitRequests.h"
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
#include <functional>

#include <optional>

#include "Search/SearchPanel.h"

#include "Settings/AppSettings.h"
#include "Settings/Panel/SettingsPanel.h"

#include "App/GraphiteMenu.h"
#include "App/LanguageServerChoice.h"
#include "App/SampleToasts.h"
#include "App/SettingsPages.h"
#include "App/ToolStripContent.h"

#include "App/Version.h"
#include "Core/App/Application.h"
#include "Core/Network/HttpClient.h"
#include "Core/App/Redraw.h"

#include "Core/Base/Log.h"

#include "Core/ImGui/DetachedWindows.h"
#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/ToastStyle.h"
#include "Core/ImGui/UiScale.h"
#include "Core/ImGui/Theme.h"

#include "Theme/ChromePalette.h"
#include "Theme/SessionPalette.h"
#include "Theme/ToastPalette.h"

#include "UI/RelativePaths.h"

#include "Core/Events/ApplicationEvent.h"

#include "UI/Dialogs/DialogLayout.h"
#include "UI/Dialogs/FileDialogOffer.h"
#include "UI/FileIcons.h"
#include "UI/InfoBars/LanguageSupportBar.h"
#include "UI/InfoBars/RestrictedModeBar.h"
#include "UI/ToastEvent.h"
#include "Workspace/WorkspaceTrust.h"

#include "Core/FileSystem/FileSystem.h"

#include "Core/Platform/Process.h"
#include "Core/Platform/Shell.h"

#include <imgui.h>

#include <chrono>
#include <cstddef>

#include <string>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {
		// WARNING: ImGuiIO::AppFocusLost cannot stand in for this: the backend sets it on focus loss, so it fires as the user leaves rather than as they come back.
		bool MainWindowHasFocus()
		{
			return Core::Application::Get().GetWindow().IsFocused();
		}

		bool IsDirectoryOnDisk(const std::filesystem::path& path)
		{
			std::error_code code;
			return std::filesystem::is_directory(path, code);
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

		// WARNING: A panel absent from every layout list has nowhere to land and opens as a window of its own.
		m_Layout.Bottom = {"console", "problems", "search", "references"};
		m_Layout.Center = {"settings", "git", "sessions"};

		m_CustomTitleBar = Core::Application::Get().GetWindow().HasCustomTitleBar();
	}

	void GraphiteLayer::OnAttach()
	{
		Core::ImGuiRenderer::SetInterfaceScale(
			static_cast<float>(m_AppSettings.GetInterfaceScalePercent()) / 100.0f);

		const Core::JobSubmitter submitToSharedQueue = [this](std::function<void()> job) { m_Jobs.Submit(std::move(job)); };
		ExplorerPanel* explorer = m_Panels.Add(
			Core::CreateScope<ExplorerPanel>(m_Workspace, m_Documents, m_AppSettings, m_Events, m_Modals, m_ActiveScheme,
											 submitToSharedQueue));
		m_Explorer = explorer;

		m_Panels.Add(Core::CreateScope<ConsolePanel>(m_Sink, m_Events, m_ActiveScheme));

		m_SearchPanel = m_Panels.Add(Core::CreateScope<SearchPanel>(
			m_Search, m_SearchSettings, m_Workspace, m_Documents, m_ActiveScheme,
			m_EditorSettings, m_PreviewSemantics));

		m_ReferencesPanel = m_Panels.Add(
			Core::CreateScope<ReferencesPanel>(m_Workspace, m_Documents, m_ActiveScheme,
											   m_EditorSettings, m_PreviewSemantics));

		m_Panels.Add(Core::CreateScope<ProblemsPanel>(
			m_Diagnostics, m_Documents, m_Workspace, m_ActiveScheme,
			[this](const std::filesystem::path& path, LanguageId language) { return m_LanguageServices.StatusFor(path, language); },
			[this](LanguageId language) { return m_LanguageServices.HasChosenServer(language); }));

		m_DiffWindows.OpenFile = [this](const std::filesystem::path& path) {
			const bool permanentTab = true;
			m_DocumentWindows.RequestOpen(path, permanentTab);
		};

		m_DiffWindows.OnCopied = [this](std::string_view text) {
			ReportCopied(m_Events, "selection", text);
		};

		m_DiffWindows.OnSetOptions = [this](const DiffViewOptions& options) { m_EditorSettings.SetDiffViewOptions(options); };

		m_ReviewWindows.OpenFile = [this](const std::filesystem::path& path) {
			const bool permanentTab = true;
			m_DocumentWindows.RequestOpen(path, permanentTab);
		};

		m_ReviewWindows.OnCopied = [this](std::string_view text) { ReportCopied(m_Events, "selection", text); };
		m_ReviewWindows.OnSetDiffOptions = [this](const DiffViewOptions& options) { m_EditorSettings.SetDiffViewOptions(options); };
		m_ReviewWindows.CopyText = [this](std::string_view what, const std::string& text) {
			CopyToClipboard(m_Events, what, text);
		};

		// WARNING: m_Toasts.OnCopied stays unset, because a Copied toast raised by copying a toast would be a toast about a toast.

		GitView* gitView = m_Panels.Add(Core::CreateScope<GitView>(m_Git, m_Work, m_GitWindowChanges, m_ActiveScheme,
																   m_EditorSettings, m_DiffWindows, m_Modals,
																   m_DocumentBuffers, m_Events));
		gitView->Subscribe();
		m_GitView = gitView;

		gitView->ReviewMenuOf = [this](const Core::Branch& branch) { return ReviewMenuOfBranch(branch); };
		gitView->OnReviewMenuItem = [this](const ReviewMenuItem& item) { ApplyReviewMenuItem(item); };

		gitView->OnBranchRenamed = [this](const std::string& from, const std::string& to) {
			m_ReviewWindows.NoteRename(from, to);
		};

		m_Panels.Add(Core::CreateScope<ChangesPanel>(m_Git, m_Modals, m_Documents,
													 m_DocumentWindows, m_ActiveScheme,
													 m_DiffWindows, m_Events, m_ChangesShared));

		m_ClaudeSettings.Load();

		m_Panels.Add(Core::CreateScope<SessionsPanel>(
			m_Workspace, m_ClaudeSettings, m_SessionWindows,
			[this](const std::string& label, const ClaudeCommand& command,
				   const std::filesystem::path& workingDirectory) {
				return m_Terminals.OpenCommand(label, command.Program, command.Arguments, workingDirectory);
			}));

		// Until a tab takes the keyboard, editor commands act on the active tab, as they always did.
		m_ActiveEditor.Set(m_DocumentWindows);

		m_Workspace.RegisterCommands(m_Commands);
		m_DocumentWindows.RegisterCommands(m_Commands, m_Keys);
		m_EditorSettings.RegisterCommands(m_Commands, m_Keys);
		m_EditorSettings.SetPictureZoom([this](PictureZoom zoom) { return m_DocumentWindows.ZoomFocusedPicture(zoom); });
		explorer->RegisterCommands(m_Commands);
		m_Panels.RegisterCommands(m_Commands);
		m_Terminals.RegisterCommands(m_Commands, m_Keys);
		RegisterApplicationCommands();

		// WARNING: Apply the overrides after every default is registered, so an override shadows the binding it replaces.
		m_AppSettings.ApplyKeyBindings(m_Keys);

		// WARNING: Set both providers before any document opens: a server choice is read at the first query for a language, and the first EnsureStarted is where a refused root must already be refused.
		m_LanguageServices.SetChoiceProvider([this](LanguageId language) { return ReadServerChoice(m_AppSettings, language); });
		m_LanguageServices.SetUninstallingQuery([this](std::string_view serverId) { return m_ServerInstalls.RunningJobOf(serverId) == InstallJobKind::Uninstall; });

		m_LanguageServices.SetRootPermission([this](const std::filesystem::path& root) { return m_Trust.IsTrusted(root); });

		// WARNING: Set the servers directory before OpenStartupFolder, whose warm-up may start a server from Graphite's install.
		m_LanguageServices.SetServersDirectory(m_ServerInstalls.GetServersDirectory());
		m_LanguageServices.SetLogDirectory(UserDirectories::LogFile().parent_path());
		m_LanguageServices.SetTemporaryFilesDirectory(UserDirectories::ServerTemporaryFilesDirectory());
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

		const GlobalState stored = m_AppSettings.GetGlobalState();

		for (const auto& [id, visible] : stored.PanelVisibility)
		{
			m_Panels.SetVisible(id, visible);
		}

		m_Terminals.SetHidden(stored.TerminalsHidden);

		m_StateKeeper.AdoptGlobal(stored);

		APP_INFO("Registered panels: {}", m_Panels.GetCount());

		explorer->LoadSettings();

		m_EditorSettings.Load();
		m_ActiveScheme.Load(m_AppSettings);
		m_SearchSettings.Load();

		// WARNING: Before OpenStartupFolder, whose restored tabs are the first files opened.
		m_Documents.LoadLargeFilesWith(submitToSharedQueue);
		m_Documents.SetTabWidthForLoads(m_EditorSettings.GetIndentStyle().Width);

		// WARNING: Load these before OpenStartupFolder sets the root: a pass begun under the default budget and then resized would evict its way down to the saved one for nothing.
		m_AnalysisSettings.Load();
		m_Analysis.SetBudget(m_AnalysisSettings.GetBudgetBytes());
		m_AnalysisCoordinator.GetPass().SetEnabled(m_AnalysisSettings.IsEnabled());

		OpenStartupFolder();

		// WARNING: Build the menu tree last, after every command is registered: the tree names ids, and one built earlier would draw disabled entries showing raw ids.
		m_MenuTree = BuildMenuTree();
	}

	void GraphiteLayer::RegisterApplicationCommands()
	{
		m_Commands.Add({.Id = "workspace.close",
						.Label = "Close Workspace",
						.Run = [this] { RequestCloseConfirmation(ClosePurpose::CloseWorkspace); },
						.Enabled = [this] { return m_Workspace.IsOpen(); }});

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
								const bool trustComesFromAnEntryAboveTheFolder = m_Trust.IsTrusted(root) && !m_Trust.IsEntry(root);
								return m_Workspace.IsOpen() && !trustComesFromAnEntryAboveTheFolder;
							},
						.Checked = [this] { return m_Workspace.IsOpen() && m_Trust.IsTrusted(m_Workspace.GetRoot()); }});

		// WARNING: Exit goes through the window's close flag, not Application::Close(), because the main loop asks OnCloseRequested only when Window::ShouldClose() is true.
		// WARNING: Alt+F4 is in the label rather than the keymap because it is Windows' shortcut and cannot be rebound.
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

		m_Commands.Add({.Id = "search.focus",
						.Label = "Find in Files",
						.Run = [this] { m_SearchPanel->Focus(); }});

		// WARNING: These commands act on the active editor view, the one last focused, not the one being drawn: which editor holds the keyboard is only knowable while drawing, and a command runs before any editor has drawn.
		RegisterEditorNavigationCommands(m_Commands, m_Keys, m_ActiveEditor,
										 EditorCommandServices{.HasLanguageServer = [this](const std::filesystem::path& path) { return m_LanguageServices.HasChosenServer(LanguageOfPath(path)); },
															   .References = m_ReferencesPanel});

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
		m_Blame.SubscribeTo(m_Events);
		m_Reviews.SubscribeTo(m_Events);

		m_Events.Subscribe<WorkspaceOpenedEvent>([this](const WorkspaceOpenedEvent& event) {
			// WARNING: Set the fallback root before RestoreState: a tab opened with a server resolves its root once and keeps it.
			m_LanguageServices.SetFallbackRoot(event.Root);

			// WARNING: A folder dropped over another replaces it without a WorkspaceClosedEvent, so everything belonging to the replaced folder is let go here, before this folder's state comes back.
			m_StateKeeper.CloseWorkspace();

			m_DocumentWindows.CarryOverOpenDocuments();

			m_SessionWindows.CarryOverOpenSessions();
			m_Terminals.CarryOverOpenTerminals();

			const bool trusted = m_Trust.IsTrusted(event.Root);

			const WorkspaceState stored = WorkspaceSettings{event.Root}.LoadState();
			const WorkspaceState restorable = RestorableState(stored, event.Root, trusted);
			m_DocumentWindows.RestoreState(restorable);
			m_SessionWindows.RestoreState(restorable.Sessions);
			m_Terminals.RestoreState(restorable.Terminals, trusted);
			m_StateKeeper.AdoptWorkspace(event.Root, stored);

			m_GitPort.SetRoot(event.Root);
			LetGoOfOldRepositoryOnWriteWorker();

			LetGoOfFolderWindows();

			// WARNING: Restore the diffs and reviews after letting go of the last folder's, never before: only the ones this
			// folder had open last time belong to it.
			m_DiffWindows.RestoreState(restorable.Diffs);

			m_ReviewStore.Open(event.Root);
			m_ReviewWindows.RestoreState(restorable.Reviews, m_ReviewStore.GetSessions());
		});

		// WARNING: Cleared on WorkspaceOpeningEvent, not WorkspaceOpenedEvent: by then the new folder has reported its own failures, and clearing would take those with it.
		m_Events.Subscribe<WorkspaceOpeningEvent>([this](const WorkspaceOpeningEvent&) {
			m_Toasts.Clear(ToastGroupOf(ToastScope::Folder));
		});

		m_Events.Subscribe<OpenFileRequested>([this](const OpenFileRequested& event) {
			m_DocumentWindows.RequestOpen(event.Path, event.Permanent);
		});

		// WARNING: OpenShellIn must not be called from inside the loop over the open terminals; this event arrives from the Explorer's menu, which draws before m_Terminals.Render.
		m_Events.Subscribe<OpenTerminalRequested>([this](const OpenTerminalRequested& event) {
			m_Terminals.Reveal();
			static_cast<void>(m_Terminals.OpenDefaultShellIn(event.Directory)); // reports its own failure
		});

		// The Git panel answers both itself; here it is only brought forward.
		m_Events.Subscribe<ShowFileHistoryRequested>([this](const ShowFileHistoryRequested&) { RevealPanel("git"); });
		m_Events.Subscribe<ShowCommitRequested>([this](const ShowCommitRequested&) { RevealPanel("git"); });

		m_Events.Subscribe<StatusMessageEvent>([this](const StatusMessageEvent& event) {
			m_DocumentWindows.SetTransientMessage(event.Text);
		});

		m_Events.Subscribe<ToastEvent>([this](const ToastEvent& event) {
			ShowToast(event.Kind, event.Title, event.Message, event.CopyText, event.Scope);
		});

		m_Events.Subscribe<WorkspaceClosedEvent>([this](const WorkspaceClosedEvent&) {
			const std::filesystem::path noRoot;

			m_LanguageServices.SetFallbackRoot(noRoot);

			// WARNING: StateKeeper writes from the last frame's snapshot, so closing the documents below does not affect what it writes.
			m_StateKeeper.CloseWorkspace();

			m_Terminals.CarryOverOpenTerminals();

			m_Documents.CloseAll();

			// WARNING: Clearing the root releases the port's cached repository handles, which otherwise block the user from deleting .git on Windows.
			m_GitPort.SetRoot(noRoot);
			LetGoOfOldRepositoryOnWriteWorker();

			LetGoOfFolderWindows();

			m_Toasts.Clear(ToastGroupOf(ToastScope::Folder));
		});

		m_Events.Subscribe<WorkspaceRootMovedEvent>([this](const WorkspaceRootMovedEvent& event) {
			m_DocumentWindows.HandleRootMoved(event.From, event.To);
			m_StateKeeper.MoveWorkspace(event.To);
			m_ReviewStore.MoveTo(event.To);
		});

		m_Events.Subscribe<WorkspaceFilesChangedEvent>(
			[this](const WorkspaceFilesChangedEvent& event) {
				m_DocumentWindows.HandleFilesChanged(event.Directories, event.Everything);
			});
	}

	void GraphiteLayer::LetGoOfOldRepositoryOnWriteWorker()
	{
		m_GitWrites.Submit([this] { m_GitPort.LetGoOfHandlesNoLongerCurrent(); });
	}

	void GraphiteLayer::LetGoOfFolderWindows()
	{
		m_DiffWindows.CloseAll();

		m_ReviewWindows.CloseAll(m_Reviews);
		m_ReviewStore.Close();
		m_ReviewedBranches.reset();

		m_DocumentWindows.ClearExpectedWrites();
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

		if (!m_Workspace.IsOpen())
		{
			static_cast<void>(m_Terminals.OpenStartupShell()); // reports its own failure
		}
	}

	void GraphiteLayer::OnUpdate(Core::Timestep timestep)
	{
		m_Workspace.Update();

		// WARNING: Update the model before the panels, so it applies whatever a worker finished this frame and any workspace event just published before a panel reads a snapshot.
		m_Git.OnUpdate(timestep);

		// WARNING: The file watcher never sees `.git/` when the workspace is a subfolder of the repository or a linked worktree, so writes made by this process are reported here.
		if (const std::uint64_t writes = m_Git.GetAppliedWriteCount(); writes != m_SeenGitWrites)
		{
			m_SeenGitWrites = writes;
			m_ChangeMarkers.Invalidate();
			m_Blame.Invalidate();
		}

		m_ChangeMarkers.OnUpdate(timestep);
		m_Blame.OnUpdate(timestep);

		FollowBranchesIntoReviews();
		m_Reviews.OnUpdate(timestep);
		m_ReviewStore.Update(timestep);

		m_Panels.OnUpdate(timestep);
		m_Terminals.Update();

		m_SolutionLoaded = m_Workspace.GetLoadedSolution().has_value();
		// WARNING: Set in OnUpdate, ahead of the Poll in OnImGuiRender, so a server started this frame starts on this selection.
		m_LanguageServices.SetSelectedSolution(m_Workspace.GetSelectedSolution());

		UpdateFocusReload(timestep);

		m_DocumentWindows.TickExpectedWrites(timestep);

		// WARNING: Watchers drop events on a buffer overflow or when a tool touches many files at once, and returning from another application is when that matters.
		m_DocumentWindows.NotifyWindowFocus(MainWindowHasFocus());

		const GlobalState globalState = CaptureGlobalState();
		const std::optional<WorkspaceState> workspaceState =
			m_Workspace.IsOpen() ? std::optional<WorkspaceState>{CaptureWorkspaceState()} : std::nullopt;

		m_StateKeeper.Observe(timestep.GetSeconds(), globalState, workspaceState);
	}

	void GraphiteLayer::UpdateFocusReload(Core::Timestep timestep)
	{
		// WARNING: Tests the process, not the main window: a panel dragged out into its own viewport is a separate OS window, and working in one is not having left the application.
		if (!Core::IsProcessInForeground())
		{
			m_SecondsInBackground += timestep.GetSeconds();
			m_AppForeground = false;
			return;
		}

		if (!m_AppForeground)
		{
			m_AppForeground = true;

			if (m_SecondsInBackground >= kReloadAfterSecondsAway && m_Workspace.IsOpen())
			{
				APP_TRACE("Reloading the workspace after {:.1f}s away", m_SecondsInBackground);
				m_Workspace.Reload(Core::IgnoreAnswers::Keep);
			}

			m_SecondsInBackground = 0.0f;
		}
	}

	void GraphiteLayer::FollowBranchesIntoReviews()
	{
		const Core::Ref<const std::vector<Core::Branch>> branches = m_Git.GetBranches();

		// WARNING: GitModel replaces its snapshot only with a list it read whole, so a snapshot that lacks a branch means the branch is gone, never that the read failed.
		if (branches == nullptr || branches == m_ReviewedBranches)
		{
			return;
		}

		m_ReviewedBranches = branches;

		for (const BranchRename& rename : m_ReviewWindows.FollowBranches(*branches, m_Reviews))
		{
			m_ReviewStore.GetSessions().Rename(rename.From, rename.To);
		}

		m_ReviewStore.GetSessions().Prune(*branches);
		m_Reviews.InvalidateRanges();
	}

	ReviewMenu GraphiteLayer::ReviewMenuOfBranch(const Core::Branch& branch) const
	{
		const Core::Ref<const std::vector<Core::Branch>> branches = m_Git.GetBranches();

		if (branches == nullptr)
		{
			return ReviewMenu{};
		}

		return ReviewMenuFor(branch, *branches, m_ReviewStore.GetSessions());
	}

	void GraphiteLayer::ApplyReviewMenuItem(const ReviewMenuItem& item)
	{
		switch (item.Action)
		{
		case ReviewAction::Start:
			m_ReviewStore.GetSessions().Start(item.Branch, item.Base);
			m_ReviewWindows.Open(item.Branch);
			break;
		case ReviewAction::Open:
			if (!item.Base.empty())
			{
				m_ReviewStore.GetSessions().OpenDraft(item.Branch, item.Base);
			}

			m_ReviewWindows.Open(item.Branch);
			break;
		case ReviewAction::End:
			m_ReviewWindows.AskToEnd(item.Branch);
			break;
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

		state.TerminalsHidden = m_Terminals.IsHidden();

		return state;
	}

	WorkspaceState GraphiteLayer::CaptureWorkspaceState() const
	{
		WorkspaceState state = m_DocumentWindows.CaptureState();
		state.Sessions = m_SessionWindows.CaptureState();
		state.Diffs = m_DiffWindows.CaptureState();
		state.Reviews = m_ReviewWindows.CaptureState();
		state.Terminals = m_Terminals.CaptureState();

		return state;
	}

	void GraphiteLayer::OnDetach()
	{
		// WARNING: Before m_Jobs is destroyed, which runs every read still queued and waits for the one running.
		m_Documents.CancelAllReads();

		m_StateKeeper.Flush();
		m_ReviewStore.Flush();

		// WARNING: Flush after StateKeeper, whose flush hands the window state to the settings.
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

	// WARNING: The focus request is applied on the panel's next Begin, which is next frame for a panel just made visible.
	void GraphiteLayer::RevealPanel(std::string_view id)
	{
		Core::Panel* panel = m_Panels.Find(id);

		if (panel == nullptr)
		{
			return;
		}

		const bool visible = true;
		m_Panels.SetVisible(id, visible);
		panel->RequestFocus();
		Core::Redraw::Request();
	}

	void GraphiteLayer::OpenLanguageSettings(LanguageId language)
	{
		m_SettingsPanel->Open({"languageServers", std::string{DefinitionOf(language).SettingsName}});
		RevealPanel("settings");
	}

	Core::ToolStripContent GraphiteLayer::BuildToolStrip(bool workingTreeHasChanges, const DiagnosticsModel::Counts& problems,
														 const Core::ChromePalette& chrome)
	{
		const EditorPalette editorPalette = ReadEditorPalette(m_ActiveScheme.Get());

		ToolStripInputs inputs{.Terminals = m_Terminals.GetStripLook(),
							   .HasChanges = workingTreeHasChanges,
							   .Errors = problems.Errors,
							   .Warnings = problems.Warnings,
							   .Colours = {.Changes = ImGui::ColorConvertFloat4ToU32(Core::ToImVec4(chrome.Accent)),
										   .Error = editorPalette.DiagnosticError,
										   .Warning = editorPalette.DiagnosticWarning,
										   .Running = ImGui::ColorConvertFloat4ToU32(Core::ToImVec4(chrome.TextDisabled))}};

		m_Panels.ForEach([this, &inputs](Core::Panel& panel) {
			const Core::Binding* binding = m_Keys.For("panel." + panel.GetId());

			inputs.Panels.push_back(ToolStripPanel{.Id = panel.GetId(),
												   .Title = panel.GetTitle(),
												   .Icon = panel.GetIcon(),
												   .MenuGroup = panel.GetMenuGroup(),
												   .Visible = panel.IsVisible(),
												   .Shortcut = binding != nullptr ? binding->Primary.ToString() : std::string()});
		});

		return BuildToolStripContent(inputs);
	}

	void GraphiteLayer::ApplyToolStripClick(std::string_view id)
	{
		if (id == kTerminalsStripId)
		{
			m_Terminals.ToggleFromStrip();
			return;
		}

		static_cast<void>(m_Panels.Toggle(id));
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
		Core::SetDetachedWindowBorderColor(palette.Border);
	}

	void GraphiteLayer::UpdateGitDecorations()
	{
		// WARNING: Status snapshots are compared by pointer: GitModel hands out a new snapshot for every status it applies, so an unchanged pointer is an unchanged status.
		if (Core::Ref<const Core::WorkingTreeStatus> status = m_Git.GetStatus(); status != m_DecoratedStatus)
		{
			m_Decorations = status ? std::make_unique<GitDecorations>(*status) : nullptr;
			++m_DecorationsVersion;
			m_DecoratedStatus = std::move(status);
		}

		GitDecorationView view;

		const bool decorationsAvailable = m_Workspace.IsOpen() && m_Decorations != nullptr;

		if (decorationsAvailable)

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
		// WARNING: Apply the chrome first, before any window is begun: ImGuiStyle::Colors must not change between a Begin and its End.
		ApplyChromeIfSchemeChanged();

		StatusMessages messages{m_DocumentWindows.GetStatusMessage(),
								m_SearchPanel->GetStatusMessage()};

		// WARNING: Read once and shared by the analysis switches, the status bar and the restricted-mode bar, because separate reads could disagree.
		const bool workspaceTrusted = m_Workspace.IsOpen() && m_Trust.IsTrusted(m_Workspace.GetRoot());

		// WARNING: Update the coordinator before Poll, so a server this starts is drained on this same frame.
		m_AnalysisCoordinator.Update(AnalysisSwitches{.Root = m_Workspace.GetRoot(),
													  .SelectedSolution = m_Workspace.GetSelectedSolution(),
													  .Trusted = workspaceTrusted,
													  .SettingEnabled = m_AnalysisSettings.IsEnabled(),
													  .TrustRevision = m_Trust.GetRevision()});

		// WARNING: Poll before anything reads a status: a status read ahead of it is always one frame stale, and on the frame a server first appears there is nothing to read at all.
		m_LanguageServices.Poll();
		PublishServerMessages();
		ApplyFinishedInstalls();

		// WARNING: Feed the diagnostics model before anything draws: a model fed later is read by the panel one frame before it is written, and the squiggle and the panel row disagree.
		DiagnosticsUpdate diagnostics;
		m_LanguageServices.TakeDiagnostics(diagnostics);
		m_Diagnostics.Apply(diagnostics);

		const std::function<bool(const std::filesystem::path&)> isOpenDocument = [this](const std::filesystem::path& path) { return IsDocumentOpen(path); };

		const DiagnosticsModel::Counts counts = m_Diagnostics.Total(isOpenDocument);
		messages.Problems = ProblemCounts{.Errors = static_cast<int>(counts.Errors), .Warnings = static_cast<int>(counts.Warnings)};
		messages.ProblemsDetail = ProblemCountText(counts.Errors, counts.Warnings);

		// WARNING: Tick before the panels draw, so what they declare is stamped with this frame's number and an entry nothing declared last frame is closed before anything asks about it.
		m_PreviewSemantics.Tick(static_cast<std::size_t>(ImGui::GetFrameCount()),
								ImGui::GetTime());

		const std::optional<std::filesystem::path> activeDocument = ActiveTextDocument(m_Documents);
		const bool foregroundBusy = activeDocument && m_DocumentWindows.IsAnalysing(*activeDocument);

		// WARNING: Tick after Poll and PreviewSemantics::Tick, because the pass asks whether a server is ready and whether a preview pane holds a document.
		// WARNING: HeldElsewhere means somebody else already holds this document with the server, not that a tab does: LanguageServices::OpenDocuments counts no references, so whichever opener sends a didClose takes the file away from all of them, and a preview pane holds no DocumentSet entry.
		m_AnalysisCoordinator.Tick(AnalysisTickInputs{
			.Now = ImGui::GetTime(),
			.ForegroundBusy = foregroundBusy,
			.BudgetBytes = m_AnalysisSettings.GetBudgetBytes(),
			.Concurrency = m_AnalysisSettings.GetConcurrency(),
			.TabWidth = m_EditorSettings.GetIndentStyle().Width,
			.HeldElsewhere = [this](const std::filesystem::path& path) { return IsDocumentOpen(path) || m_PreviewSemantics.Holds(path); }});

		if (activeDocument)
		{
			m_LanguageServices.EnsureStarted(*activeDocument, LanguageOfPath(*activeDocument));
		}

		// WARNING: The bar describes the document on screen, not the active one, which outlives its tab being covered by another.
		const std::function<bool(const std::filesystem::path&)> isShown = [this](const std::filesystem::path& path) { return IsDocumentShown(path); };
		const std::optional<std::size_t> shownIndex = ShownActiveDocument(m_Documents, isShown);
		const std::optional<std::filesystem::path> shownDocument =
			shownIndex ? std::optional{m_Documents.At(*shownIndex).Path} : std::nullopt;

		const std::optional<std::size_t> shownTab = ShownActiveTab(m_Documents, isShown);
		m_Explorer->SetShownFile(shownTab ? m_Documents.At(*shownTab).Path : std::filesystem::path{});

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

			// WARNING: ServerChoice is copied without its overrides, which hold JSON that would be copied every frame.
			languagePrompt = LanguagePromptFor(LanguagePromptInputs{
				.Language = activeLanguage,
				.Choice = ServerChoice{.Kind = choice.Kind, .Server = choice.Server},
				.State = status.State,
				.Install = choice.Server != nullptr ? InstallInputsFor(m_ServerInstalls, *choice.Server) : InstallInputs{},
				.Dismissed = m_LanguagePromptsDismissedUntilRestart.contains(activeLanguage),
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

		if (shownDocument)
		{
			if (const std::optional<ServerReport> report = m_LanguageServices.ReportFor(*shownDocument, activeLanguage))
			{
				const std::chrono::minutes localOffset = Core::OffsetAt(
					m_LocalTimeZone, std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));

				serverInputs.ActiveReport = ServerReportText(*report, localOffset);
			}
		}

		if (const ServerDefinition* chosen = m_LanguageServices.ChosenServer(activeLanguage); shownDocument && chosen != nullptr)
		{
			serverInputs.ActiveJob = ServerJobFor(m_ServerInstalls, *chosen);
		}

		std::optional<LanguageId> serverSettingsLanguage;

		if (std::optional<ServerLine> serverLine = ChooseServerLine(serverInputs))
		{
			messages.Server = std::move(serverLine->Text);
			messages.ServerDetail = std::move(serverLine->Detail);
			messages.ServerOpensSettings = serverLine->SettingsLanguage.has_value();
			messages.Health = serverLine->Health;
			serverSettingsLanguage = serverLine->SettingsLanguage;
		}

		if (std::string background = BackgroundAnalysisText(m_AnalysisCoordinator.GetPass().Progress());
			!background.empty())
		{
			messages.Background = std::move(background);
			messages.BackgroundDetail = BackgroundAnalysisDetail(
				m_AnalysisCoordinator.GetPass().Progress(), m_Analysis.GetBytes(), m_Analysis.GetBudget());
		}

		if (m_Explorer != nullptr)
		{
			messages.ExplorerFileOperation = m_Explorer->FileOperationText();
		}

		bool workingTreeHasChanges = false;

		{
			const Core::Ref<const Core::HeadState> head = m_Git.GetHead();
			const Core::Ref<const std::vector<Core::Branch>> branches = m_Git.GetBranches();
			const Core::Ref<const Core::WorkingTreeStatus> gitStatus = m_Git.GetStatus();
			const GitPanelWait gitWait = m_GitView != nullptr ? m_GitView->CurrentWait() : GitPanelWait{};

			const GitStatusInputs gitStatusInputs{
				.FolderOpen = m_Workspace.IsOpen(),
				.MissingRepository = IsMissingRepository(gitStatus != nullptr, m_Git.GetStatusError()),
				.Head = head.get(),
				.Branches = branches.get(),
				.Status = gitStatus.get(),
				.CheckingOut = gitWait.CheckingOut,
				.Activity = DescribeGitActivity(m_Git.GetWriteReport(), gitWait.AwaitingAnswer),
			};

			messages.Git = GitStatusText(gitStatusInputs);

			workingTreeHasChanges = gitStatus != nullptr && !gitStatus->Entries.empty();
		}

		// WARNING: Drain the departures before the toasts draw, so a toast cleared with its folder since the last frame already counts for the bell this frame.
		for (const Core::ToastDeparture& departure : m_Toasts.TakeDepartures())
		{
			m_Notifications.NoteDeparture(departure);
		}

		// WARNING: The popup marks everything seen when it draws, after the bar, so the dot would flash for a frame while it is open.
		const std::optional<Core::ToastKind> missedKind = m_Notifications.MostSevereMissed();
		const bool missedOutsideTheOpenList = missedKind.has_value() && !m_NotificationsPopup.IsOpen();
		if (missedOutsideTheOpenList)
		{
			messages.MissedNotificationColour = Core::ToastAccentOf(ToastPaletteFor(m_ActiveScheme.Get()), *missedKind);
		}

		const EditorPalette statusPalette = ReadEditorPalette(m_ActiveScheme.Get());
		messages.Colours = StatusBarColours{.Error = statusPalette.DiagnosticError,
											.Warning = statusPalette.DiagnosticWarning,
											.Ready = ServerReadyColour(ChromePaletteFor(m_ActiveScheme.Get()).Background)};

		messages.View = m_DocumentWindows.GetViewStatusItems();

		switch (DrawStatusBar(m_Documents, shownIndex, messages))
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

		const Core::ChromePalette chromePalette = ChromePaletteFor(m_ActiveScheme.Get());
		const Core::ToolStripContent toolStrip = BuildToolStrip(workingTreeHasChanges, counts, chromePalette);

		const Core::MenuBarHeights menuHeights = m_CustomTitleBar ? TitleMenuHeights() : Core::MenuBarHeights{};
		m_Layout.MenuBarHeight = menuHeights.Row;
		m_Layout.StripWidth = Core::ToolStripWidth();
		m_DockSpace.Begin(
			m_Layout,
			[this, workspaceTrusted, &languagePrompt] {
				RenderRestrictedModeBar(workspaceTrusted);

				if (languagePrompt)
				{
					RenderLanguageSupportBar(*languagePrompt);
				}
			},
			[this, &toolStrip, &chromePalette] {
				if (const std::optional<std::string> clicked = Core::DrawToolStrip(toolStrip, chromePalette))
				{
					ApplyToolStripClick(*clicked);
				}
			});
		TitleContext titleContext;

		if (m_Workspace.IsOpen())
		{
			titleContext.Folder = FolderLabel(m_Workspace.GetRoot());
		}

		m_MenuBar.Render(m_Commands, m_Keys, m_MenuTree, menuHeights, [this] { m_TitleBar.RenderLogo(Core::Application::Get().GetWindow()); }, [this, &titleContext] { m_TitleBar.Render(Core::Application::Get().GetWindow(), titleContext); });
		RenderEmptyEditorArea();
		m_DockSpace.End();

		// WARNING: Open modal popups outside the dockspace host's Begin/End and every other window: a popup must not be opened from inside a window that may not be current when it needs to draw.
		RenderCloseConfirm();
		RenderDropConfirm();
		m_About.Render();
		RenderTrustParentConfirm();

		UpdateGitDecorations();

		// WARNING: Panels render outside the host window, because inside it they would dock as ordinary child windows instead of landing in the dockspace nodes.
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
											 .HighlightOccurrences = m_EditorSettings.IsHighlightingOccurrences(),
											 .LineAuthor = m_EditorSettings.HasLineAuthor(),
											 .Hover = m_EditorSettings.GetHoverSettings(),
											 .Scheme = m_ActiveScheme.Get(),
											 .CloseRequests = m_TabCloseRequests,
											 .WorkspaceRoot = m_Workspace.GetRoot(),
											 .Settings = m_EditorSettings},
								 *m_ReferencesPanel);

		m_DiffWindows.Render(m_Git, ReadGitPalette(m_ActiveScheme.Get()),
							 ReadSyntaxPalette(m_ActiveScheme.Get()),
							 ReadEditorPalette(m_ActiveScheme.Get()).Selection,
							 m_EditorSettings.GetDiffViewOptions(), m_DockSpace.GetCenterNodeId(),
							 m_TabCloseRequests);

		const GitPalette reviewPalette = ReadGitPalette(m_ActiveScheme.Get());
		const SyntaxPalette reviewSyntax = ReadSyntaxPalette(m_ActiveScheme.Get());

		m_ReviewWindows.Render(ReviewRenderInput{.Model = m_Reviews,
												 .Sessions = m_ReviewStore.GetSessions(),
												 .Danger = DangerColourOf(m_ActiveScheme.Get()),
												 .Palette = reviewPalette,
												 .Syntax = reviewSyntax,
												 .Selection = ReadEditorPalette(m_ActiveScheme.Get()).Selection,
												 .DiffOptions = m_EditorSettings.GetDiffViewOptions(),
												 .CentreNode = m_DockSpace.GetCenterNodeId(),
												 .CloseRequests = m_TabCloseRequests});

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

		// WARNING: Drained here, not in a git panel, so a failure is reported with both panels closed and only once with both open.
		while (std::optional<GitModel::WriteFailure> failure = m_Git.TakeWriteError())
		{
			std::string title(WriteFailureTitle(failure->Kind));
			std::string shown = RelativiseUnder(failure->Error.Message, m_Git.GetWorkingDirectory());
			std::string copied = title + "\n" + failure->Error.Message;

			ShowToast(Core::ToastKind::Error, std::move(title), std::move(shown), std::move(copied), ToastScope::Folder);
		}

		// WARNING: Draw the toasts last, so they sit over every panel and terminal.
		const Core::ToastPalette toastPalette = ToastPaletteFor(m_ActiveScheme.Get());
		m_NotificationsPopup.Render(m_Notifications, toastPalette, m_Workspace.GetRoot(), m_LocalTimeZone);
		m_Toasts.Render(toastPalette);

		// WARNING: End the frame of close requests after every owner has drawn: what nobody took belonged to a window that went some other way.
		m_TabCloseRequests.EndFrame();

		// WARNING: Clear the input only here, the last point in the frame that reads the queue.
		m_FrameInput.Clear();

		// WARNING: Flush last, after everything that can change a setting this frame, so one frame's changes make one write.
		m_AppSettings.Flush();
	}

	void GraphiteLayer::OnEvent(Core::Event& event)
	{
		// WARNING: Handle the drop before the panels: it is not input anybody competes for, and routing it through the shortcut path would only let the panels swallow it.
		HandleFileDrop(event);

		if (event.Handled)
		{
			return;
		}

		// WARNING: Panels see the event before the router: a chord being recorded is consumed there, and capturing Ctrl+Shift+S would otherwise bind it and save everything in the same keystroke.
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
				ClassifyDrop(dropped.GetPaths(), IsDirectoryOnDisk);

			// WARNING: Open the workspace before the files, so a file dropped with its folder opens into that folder rather than the one being replaced; a question being asked does not hold the files back.
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

			const bool permanentTab = true;

			for (const std::filesystem::path& file : routing.Files)
			{
				m_Workspace.RequestOpenFile(file, permanentTab);
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
			// OpenFolder reports its own failures.
			static_cast<void>(m_Workspace.OpenFolder(dropped.Root));
			break;

		case DropTarget::Solution:
			// Logged only: OpenSolution reports every failure in a toast itself.
			if (const Core::Result<void> opened = m_Workspace.OpenSolution(dropped.Root); !opened)
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
		if (!m_Workspace.IsOpen())
		{
			ImGui::BeginDisabled(m_Workspace.IsDialogOpen());
			const bool clicked = m_DockSpace.CenterEmptyState("Open a folder to start working, or drop a file here.",
															  "Open Folder", kFileDialogOpenNote);
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
			m_LanguagePromptsDismissedUntilRestart.insert(prompt.Language);
			break;
		}
	}

	void GraphiteLayer::RenderRestrictedModeBar(bool trusted)
	{
		if (!m_Workspace.IsOpen() || trusted)
		{
			return;
		}

		// WARNING: Copy the root: trusting writes the list, and nothing here should hold a reference into state that the click is about to change.
		const std::filesystem::path root = m_Workspace.GetRoot();

		if (m_RestrictedBarsHiddenUntilRestart.contains(root))
		{
			return;
		}

		const std::optional<std::filesystem::path> parent = ParentWorthTrusting(root);
		const ImVec4 warningColour = ImGui::ColorConvertU32ToFloat4(ReadEditorPalette(m_ActiveScheme.Get()).DiagnosticWarning);

		switch (DrawRestrictedModeBar(parent, warningColour))
		{
		case RestrictedBarChoice::None:
			break;

		case RestrictedBarChoice::Close:
			m_RestrictedBarsHiddenUntilRestart.insert(root);
			break;

		case RestrictedBarChoice::TrustFolder:
			m_Trust.Trust(root);
			break;

		case RestrictedBarChoice::TrustParent:
			m_TrustParentQuestion.Ask(*parent);
			break;
		}
	}

	void GraphiteLayer::RenderTrustParentConfirm()
	{
		DialogLayout::BeforeBegin(DialogLayout::kNarrowWidth);

		const std::filesystem::path* asked = m_TrustParentQuestion.Begin([this](const std::filesystem::path& parent) {
			if (!m_Workspace.IsOpen())
			{
				return false;
			}

			const std::filesystem::path root = m_Workspace.GetRoot();
			return !m_Trust.IsTrusted(root) && ParentWorthTrusting(root) == parent;
		});

		DialogLayout::AfterBegin();

		if (asked == nullptr)
		{
			return;
		}

		// WARNING: Copy the parent out: Answer clears what `asked` points at, and trusting writes the list.
		const std::filesystem::path parent = *asked;

		DialogLayout::Text(TrustAllMessage(parent));
		DialogLayout::Gap();

		const DialogLayout::ButtonSpec noSafeButton{};
		const DialogLayout::ButtonSpec trustAllButton{.Label = "Trust All"};
		const DialogLayout::Choice choice =
			DialogLayout::ButtonRow(noSafeButton, trustAllButton, DangerColourOf(m_ActiveScheme.Get()));

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
		DialogLayout::BeforeBegin(DialogLayout::kNarrowWidth);
		const DroppedWorkspace* asked = m_DropQuestion.Begin();
		DialogLayout::AfterBegin();

		if (asked == nullptr)
		{
			return;
		}

		// WARNING: Copy the dropped workspace out before anything is opened: Answer clears what `asked` points at.
		const DroppedWorkspace dropped = *asked;

		DialogLayout::Text("Open this in place of the workspace now open?");
		DialogLayout::Gap();
		DialogLayout::MutedText(Core::ToUtf8(dropped.Root));
		DialogLayout::Gap();

		const DialogLayout::Choice choice = DialogLayout::ButtonRow({.Label = "Open"});

		if (choice == DialogLayout::Choice::Safe)
		{
			OpenDropped(dropped);
			m_DropQuestion.Answer();
		}
		else if (choice == DialogLayout::Choice::Cancel)
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

		dispatcher.Dispatch<Core::MouseButtonPressedEvent>([this](Core::MouseButtonPressedEvent&) {
			m_Router.CancelTap();
			return false;
		});
	}

	void GraphiteLayer::QueueForFocusedEditor(Core::Event& event)
	{
		// WARNING: Queue rather than hand the event to an editor here: which document holds the keyboard is only knowable while drawing, and by then the event is gone.
		// WARNING: Both handlers return false: the queue is read by at most one focused editor, so reporting the key as consumed would claim it landed somewhere.
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
		// WARNING: Re-checking for dirty documents after Save All or Discard All would reopen the modal on Discard All, which never touches document content.
		if (m_CloseApproved)
		{
			return true;
		}

		// WARNING: Any unresolved close question blocks this, not only this purpose's: Alt+F4 reaches here whatever ImGui thinks holds input focus, so testing the purpose would overwrite a pending workspace close.
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

		// WARNING: One close question at a time, whatever raised it: Ask queues rather than replaces, so a second Close Workspace would close the workspace twice.
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
			// WARNING: OnCloseRequested refusing earlier, to raise this very modal, cleared Window's close flag for good, and nothing but this call re-raises it.
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

		// WARNING: The question went stale while it waited its turn but the request behind it did not, so the close goes ahead.
		FinishClose(purpose);
		return false;
	}

	bool GraphiteLayer::IsDocumentOpen(const std::filesystem::path& path) const
	{
		return m_Documents.GetStore().Find(path) != nullptr;
	}

	bool GraphiteLayer::IsDocumentShown(const std::filesystem::path& path) const
	{
		return m_DocumentWindows.IsShown(path);
	}

	void GraphiteLayer::ListModifiedDocuments() const
	{
		std::vector<std::filesystem::path> modified;

		for (const std::shared_ptr<Core::TextDocument>& document : m_Documents.GetStore().GetDocuments())
		{
			if (document->IsModified())
			{
				modified.push_back(document->GetPath());
			}
		}

		constexpr std::size_t kNoHeaders = 0;
		const std::string noTag;

		if (DialogLayout::BeginFileList("close.modified", modified.size(), kNoHeaders))
		{
			for (const std::filesystem::path& path : modified)
			{
				DialogLayout::FileRow(KindOfFile(path), Core::ToUtf8(path.filename()), noTag);
			}

			// WARNING: The child must end on an item even should the list be empty.
			if (modified.empty())
			{
				ImGui::Dummy(ImVec2(0.0f, 0.0f));
			}
		}

		DialogLayout::EndFileList();
	}

	void GraphiteLayer::RenderCloseConfirm()
	{
		DialogLayout::BeforeBegin(DialogLayout::kNarrowWidth);
		const ClosePurpose* asked =
			m_CloseQuestion.Begin([this](ClosePurpose purpose) { return ConfirmationStillNeeded(purpose); });
		DialogLayout::AfterBegin();

		if (asked == nullptr)
		{
			return;
		}

		// WARNING: Copied out before Answer(), which drops the payload `asked` points at.
		const ClosePurpose purpose = *asked;

		DialogLayout::Text("These files have unsaved changes:");
		DialogLayout::Gap();

		ListModifiedDocuments();

		DialogLayout::Gap();

		const DialogLayout::Choice choice = DialogLayout::ButtonRow(
			{.Label = "Save All"}, {.Label = "Discard All"}, DangerColourOf(m_ActiveScheme.Get()));

		if (choice == DialogLayout::Choice::Safe)
		{
			// WARNING: SaveAllModified returns false for a stopped batch and for one that skipped a conflicted document, so the modal stays up rather than closing on a promise it did not keep.
			if (m_DocumentWindows.SaveAllModified())
			{
				FinishClose(purpose);
				m_CloseQuestion.Answer();
			}
		}
		else if (choice == DialogLayout::Choice::Destructive)
		{
			FinishClose(purpose);
			m_CloseQuestion.Answer();
		}
		else if (choice == DialogLayout::Choice::Cancel)
		{
			m_CloseQuestion.Answer();
		}

		m_CloseQuestion.End();
	}
}
