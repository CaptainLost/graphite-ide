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
#include "Git/Worktrees/WorktreeOfFile.h"
#include "Git/GitStatusText.h"
#include "Git/Branches/BranchAuthors.h"
#include "Git/Branches/BranchLookup.h"
#include "Git/GitView.h"
#include "Git/History/HistoryPlaceholder.h"
#include "Git/Network/NetworkCommands.h"
#include "Git/Network/NetworkText.h"
#include "Git/WriteFailureTitle.h"

#include "Console/ConsolePanel.h"

#include "Explorer/ExplorerPanel.h"

#include "Language/Diagnostics/ProblemsPanel.h"
#include "Search/ReferencesPanel.h"
#include "Sessions/SessionsPanel.h"
#include "Terminal/TerminalWindows.h"

#include <format>
#include <functional>

#include <optional>

#include "Search/SearchPanel.h"

#include "Settings/AppSettings.h"
#include "Settings/Panel/SettingsPanel.h"

#include "App/FolderParticipants.h"
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
		  m_TrustParentQuestion(m_Modals, kTrustParentTitle),
		  m_ForcePushQuestion(m_Modals, kForcePushTitle)
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

		m_FolderReviews.Windows().OpenFile = [this](const std::filesystem::path& path) {
			const bool permanentTab = true;
			m_DocumentWindows.RequestOpen(path, permanentTab);
		};

		m_FolderReviews.Windows().OnCopied = [this](std::string_view text) { ReportCopied(m_Events, "selection", text); };
		m_FolderReviews.Windows().OnSetDiffOptions = [this](const DiffViewOptions& options) { m_EditorSettings.SetDiffViewOptions(options); };
		m_FolderReviews.Windows().CopyText = [this](std::string_view what, const std::string& text) {
			CopyToClipboard(m_Events, what, text);
		};

		// WARNING: The toasts' OnCopied stays unset, because a Copied toast raised by copying a toast would be a toast about a toast.

		GitView* gitView = m_Panels.Add(Core::CreateScope<GitView>(m_Git, m_Lines, m_Network, m_Worktrees, m_Content, m_GitWindowChanges,
																   m_WorktreeChanges, m_WorktreeWindowChanges,
																   m_ActiveScheme, m_EditorSettings, m_DiffWindows, m_Modals,
																   m_DocumentBuffers, m_Events));
		gitView->Subscribe();
		m_GitView = gitView;

		gitView->ReviewMenuOf = [this](const Core::Branch& branch) { return m_FolderReviews.MenuOf(branch); };
		gitView->OnReviewMenuItem = [this](const ReviewMenuItem& item) { m_FolderReviews.Apply(item); };

		gitView->OnBranchRenamed = [this](const std::string& from, const std::string& to) {
			m_FolderReviews.NoteRename(from, to);
		};

		// WARNING: Fetch, Pull and Push run git, which runs the folder's hooks and the commands its config names, so a
		// folder in restricted mode runs none of them (ADR 0678 amends ADR 0532).
		gitView->IsFolderTrusted = [this] { return IsOpenFolderTrusted(); };

		m_Panels.Add(Core::CreateScope<ChangesPanel>(m_Git, m_Modals, m_DocumentBuffers, m_ActiveScheme, m_DiffWindows,
													 m_Events, m_ChangesShared));

		m_ClaudeSettings.Load();

		m_SessionsPanel = m_Panels.Add(Core::CreateScope<SessionsPanel>(
			m_Workspace, m_ClaudeSettings, m_SessionWindows,
			[this](const std::string& label, const ClaudeCommand& command,
				   const std::filesystem::path& workingDirectory) {
				return m_Terminals.OpenCommand(label, command.Program, command.Arguments, workingDirectory);
			}));

		// Until a tab takes the keyboard, editor commands act on the active tab, as they always did.
		m_ActiveEditor.Set(m_DocumentWindows);

		m_Workspace.RegisterCommands(m_Commands);
		m_DocumentWindows.RegisterCommands(m_Commands, m_Keys);

		// The list is null while unread: until then every file belongs to the open folder's tab naming.
		m_DocumentWindows.SetWorktreeOf([this](const std::filesystem::path& file) -> std::optional<std::string> {
			const std::vector<Core::WorktreeEntry>* entries = m_Worktrees.Entries();
			return entries == nullptr ? std::nullopt : WorktreeFolderOf(file, *entries, m_Workspace.GetRoot());
		});
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

		m_Workspace.SetReplaceGate([this](std::function<void()> open) { AskBeforeReplacingFolder(std::move(open)); });

		const GlobalState stored = m_AppSettings.GetGlobalState();

		for (const auto& [id, visible] : stored.PanelVisibility)
		{
			m_Panels.SetVisible(id, visible);
		}

		m_Terminals.SetHidden(stored.TerminalsHidden);

		m_StateKeeper.AdoptGlobal(stored);

		APP_TRACE("Registered panels: {}", m_Panels.GetCount());

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

		// WARNING: Put back before OpenStartupFolder, so a restored tab of the folder takes the document holding the unsaved
		// changes instead of reading the file, and a large file is never read twice.
		const PutBackUnsaved putBack = PutBack(m_Documents.GetStore(), m_UnsavedCopies.ClaimLeftBehind());

		OpenStartupFolder();

		ShowPutBackUnsaved(putBack);

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

		RegisterNetworkCommands(m_Commands,
								NetworkCommandServices{.Buttons = [this] { return DescribeNetworkButtonsNow(m_Git, m_Network, m_GitView != nullptr && m_GitView->CurrentWait().CheckingOut.has_value(), IsOpenFolderTrusted()); },
													   .Fetch = [this] { m_Network.Fetch(); },
													   .Pull = [this] { m_Network.Pull(); },
													   .Push = [this] { m_Network.Push(); },
													   .CanForcePush = [this] { return DivergedBranch().has_value(); },
													   .ForcePush = [this] {
														   if (const std::optional<ForcePushQuestion> diverged = DivergedBranch())
														   {
															   m_ForcePushQuestion.Ask(*diverged);
														   }
													   }});

		m_Commands.Add({.Id = "search.focus",
						.Label = "Find in Files",
						.Run = [this] { m_SearchPanel->Focus(); }});

		m_Keys.SetDefault("search.focus", {.Primary = Core::Chord::Key(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_F)});

		m_Commands.Add({.Id = "file.quickOpen",
						.Label = "Go to File...",
						.Run = [this] { m_QuickOpen.Open(m_Workspace.GetRoot()); },
						.Enabled = [this] { return m_Workspace.IsOpen(); },
						.RepeatsWhileHeld = false});

		m_Keys.SetDefault("file.quickOpen", {.Primary = Core::Chord::Key(ImGuiMod_Ctrl | ImGuiKey_P)});

		m_Commands.Add({.Id = "view.commandPalette",
						.Label = "Command Palette...",
						.Run = [this] { m_CommandPalette.Open(ListPaletteEntries(m_Commands, m_Keys, "view.commandPalette")); },
						.RepeatsWhileHeld = false});

		m_Keys.SetDefault("view.commandPalette", {.Primary = Core::Chord::Key(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P)});

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

		// WARNING: Before AddFolderParticipants, so the lines model is cleared before the port is pointed at the next
		// folder: a read asked for the folder before must find it cleared, or it would write the next folder's line store.
		m_Lines.SubscribeTo(m_Events);
		m_Worktrees.SubscribeTo(m_Events);
		m_Content.SubscribeTo(m_Events);
		m_ChangeMarkers.SubscribeTo(m_Events);
		m_Blame.SubscribeTo(m_Events);
		m_Reviews.SubscribeTo(m_Events);

		// WARNING: Before AddFolderParticipants, so a running fetch, pull or push has ended before the port is pointed at the
		// next folder.
		m_Network.SubscribeTo(m_Events);

		AddFolderParticipants();

		m_Events.Subscribe<WorkspaceOpenedEvent>([this](const WorkspaceOpenedEvent& event) { m_Folder.Open(event.Root); });

		// WARNING: Cleared on WorkspaceOpeningEvent, not WorkspaceOpenedEvent: by then the new folder has reported its own failures, and clearing would take those with it.
		m_Events.Subscribe<WorkspaceOpeningEvent>([this](const WorkspaceOpeningEvent&) {
			m_Notifications.ClearFolder();
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
			m_Notifications.Show(event, m_Workspace.GetRoot());
		});

		m_Events.Subscribe<WorkspaceClosedEvent>([this](const WorkspaceClosedEvent&) {
			m_Folder.Close();
			m_Notifications.ClearFolder();
		});

		m_Events.Subscribe<WorkspaceRootMovedEvent>(
			[this](const WorkspaceRootMovedEvent& event) { m_Folder.Move(event.From, event.To); });

		m_Events.Subscribe<PathMovedEvent>(
			[this](const PathMovedEvent& event) { m_DocumentWindows.FollowMove(event.From, event.To); });

		m_Events.Subscribe<WorkspaceFilesChangedEvent>(
			[this](const WorkspaceFilesChangedEvent& event) {
				m_DocumentWindows.HandleFilesChanged(event.Directories, event.Everything);
			});
	}

	void GraphiteLayer::LetGoOfOldRepositoryOnWriteAndNetworkWorkers()
	{
		m_GitWrites.Submit([this] { m_GitPort.LetGoOfHandlesNoLongerCurrent(); });
		m_NetworkJobs.Submit([this] { m_GitPort.LetGoOfHandlesNoLongerCurrent(); });
	}

	void GraphiteLayer::PointWorktreePortAt(const std::filesystem::path& root)
	{
		m_WorktreeGitPort.SetRoot(root);

		// WARNING: m_Jobs has several workers and this reaches one of them; each of the others lets go on its next query
		// of this port, so it may hold the worktree before until another worktree is looked at.
		m_Jobs.Submit([this] { m_WorktreeGitPort.LetGoOfHandlesNoLongerCurrent(); });
		m_GitWrites.Submit([this] { m_WorktreeGitPort.LetGoOfHandlesNoLongerCurrent(); });
	}

	std::int64_t GraphiteLayer::SystemClockSeconds()
	{
		return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	}

	void GraphiteLayer::AddFolderParticipants()
	{
		// WARNING: The order is the order of every transition. The servers' root comes before the documents, which
		// resolve their server's root once when restored and keep it; the saved state comes before the documents,
		// since it writes from the last frame's snapshot and must not see them closed.
		m_Folder.Add(Core::CreateScope<ServerRootOfFolder>(m_LanguageServices));
		m_Folder.Add(Core::CreateScope<SavedStateOfFolder>(m_StateKeeper));
		m_Folder.Add(Core::CreateScope<DocumentsOfFolder>(m_DocumentWindows, m_Documents));
		m_Folder.Add(Core::CreateScope<SessionsOfFolder>(m_SessionWindows));
		m_Folder.Add(Core::CreateScope<TerminalsOfFolder>(m_Terminals));
		m_Folder.Add(Core::CreateScope<GitWindowOfFolder>(*m_GitView));
		m_Folder.Add(Core::CreateScope<RepositoryOfFolder>(m_GitPort, [this] { LetGoOfOldRepositoryOnWriteAndNetworkWorkers(); }));
		m_Folder.Add(Core::CreateScope<DiffsOfFolder>(m_DiffWindows));
		m_Folder.Add(Core::CreateScope<ReviewsOfFolder>(m_FolderReviews));
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

	void GraphiteLayer::ShowPutBackUnsaved(const PutBackUnsaved& putBack)
	{
		constexpr std::size_t kNamesShown = 4;

		const auto listNames = [](const std::vector<std::string>& names) {
			std::string listed;

			for (std::size_t index = 0; index < names.size() && index < kNamesShown; ++index)
			{
				listed += index == 0 ? "" : ", ";
				listed += names[index];
			}

			if (names.size() > kNamesShown)
			{
				listed += std::format(" and {} more", names.size() - kNamesShown);
			}

			return listed;
		};

		std::vector<std::string> putBackNames;

		for (const std::shared_ptr<Core::TextDocument>& document : putBack.Documents)
		{
			m_DocumentWindows.OpenPutBack(document->GetPath());

			if (document->IsModified())
			{
				putBackNames.push_back(Core::ToUtf8(document->GetPath().filename()));
			}
		}

		if (!putBackNames.empty())
		{
			m_Events.Publish(ToastEvent{
				.Kind = Core::ToastKind::Warning,
				.Title = "Unsaved changes brought back",
				.Message = std::format("Graphite did not close normally last time. The unsaved changes to {} are back in "
									   "their tabs; Undo there goes back to the file on disk.",
									   listNames(putBackNames))});
		}

		for (const PutBackUnsaved::Failure& failure : putBack.Failures)
		{
			const std::string message = std::format("{}. The changes are kept in {} for the next launch.", failure.Reason,
													Core::ToUtf8(failure.Copy.KeptIn));

			m_Events.Publish(ToastEvent{.Kind = Core::ToastKind::Error,
										.Title = std::format("Cannot bring back the unsaved changes to {}",
															 Core::ToUtf8(failure.Copy.Path.filename())),
										.Message = message,
										.CopyText = message});
		}
	}

	void GraphiteLayer::OnUpdate(Core::Timestep timestep)
	{
		if (std::function<void()> replace = std::exchange(m_ApprovedReplace, {}))
		{
			replace();
		}

		m_Workspace.Update();

		// WARNING: Before the panels, so the models apply whatever a worker finished this frame and any workspace event just
		// published before a panel reads a snapshot.
		m_Repository.Update(timestep);
		ApplyNetworkResults();

		// The worktree the Git window looks at. Its watch keeps it fresh, and the worktree counts with it.
		m_LookedAtWorktree.Update(timestep);

		while (std::optional<WorktreeWriteFailure> failure = m_WorktreeChanges.TakeWriteError())
		{
			ReportGitWriteFailure(*failure);
		}

		m_ChangeMarkers.OnUpdate(timestep);
		m_Blame.OnUpdate(timestep);

		m_FolderReviews.Update(timestep);

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
			m_Workspace.IsOpen() ? std::optional<WorkspaceState>{m_Folder.Capture()} : std::nullopt;

		m_StateKeeper.Observe(timestep.GetSeconds(), globalState, workspaceState);
		m_UnsavedCopies.Observe(timestep.GetSeconds(), m_Documents.GetStore());
	}

	void GraphiteLayer::UpdateFocusReload(Core::Timestep timestep)
	{
		// WARNING: Tests the process, not the main window: a panel dragged out into its own viewport is a separate OS window, and working in one is not having left the application.
		if (!Core::IsProcessInForeground())
		{
			// The moment the reader turns to another application, not every frame after it.
			if (m_AppForeground && m_EditorSettings.SavesOnFocusLoss())
			{
				static_cast<void>(m_DocumentWindows.SaveAllModified()); // reports its own failures
			}

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

	void GraphiteLayer::OnDetach()
	{
		// WARNING: Before m_NetworkJobs is destroyed, which waits for the job running: a fetch nobody cancels can run for
		// as long as the network makes it.
		m_Network.Detach();

		// WARNING: Before m_Jobs is destroyed, which runs every read still queued and waits for the one running.
		m_Documents.CancelAllReads();

		// Saves the content checks' verdicts by a job, which the queue runs before its workers stop.
		m_Content.Clear();

		m_StateKeeper.Flush();
		m_FolderReviews.Flush();

		// The reader answered for every unsaved change when the close question was answered or nothing was modified; an
		// exit nobody asked about, such as the session ending, keeps them for the next launch.
		if (m_CloseApproved || !m_DocumentWindows.HasModifiedDocuments())
		{
			m_UnsavedCopies.Discard();
		}
		else
		{
			m_UnsavedCopies.KeepForNextLaunch(m_Documents.GetStore());
		}

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
		// WARNING: Released before anything draws: the editor or terminal holding the keyboard claims it again as it draws, and one that stopped drawing must stop holding it.
		m_Typing.Release();

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
		// WARNING: HeldElsewhere counts a document a tab has open but has not drawn yet: that tab will analyse it itself,
		// and the pass's copy, read from disk, may not be what the tab shows.
		m_AnalysisCoordinator.Tick(AnalysisTickInputs{
			.Now = ImGui::GetTime(),
			.ForegroundBusy = foregroundBusy,
			.BudgetBytes = m_AnalysisSettings.GetBudgetBytes(),
			.Concurrency = m_AnalysisSettings.GetConcurrency(),
			.TabWidth = m_EditorSettings.GetIndentStyle().Width,
			.HeldElsewhere = [this](const std::filesystem::path& path) {
				return IsDocumentOpen(path) || m_DocumentHolders.IsHeldByAnyBut(DocumentHolder::Analysis, path);
			}});

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

		const Core::IndentStyle indent = m_EditorSettings.GetIndentStyle();
		m_Explorer->SetIndent(indent.UseSpaces ? std::string(indent.Width, ' ') : std::string{"\t"});

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

			// A stage or commit in the worktree the Git window looks at is said too, naming its folder.
			const std::string worktreeFolder =
				Core::ToUtf8(Core::NormalizeDirectory(m_WorktreeChanges.Worktree().value_or(std::filesystem::path{})).filename());

			const GitStatusInputs gitStatusInputs{
				.FolderOpen = m_Workspace.IsOpen(),
				.MissingRepository = IsMissingRepository(gitStatus != nullptr, m_Git.GetStatusError()),
				.Head = head.get(),
				.Branches = branches.get(),
				.Status = gitStatus.get(),
				.CheckingOut = gitWait.CheckingOut,
				.Activity = WithWorktreeWrite(DescribeGitActivity(m_Git.GetWriteReport(), gitWait.AwaitingAnswer,
																  DescribeNetworkActivity(m_Network.GetState(), m_Network.GetShownDone())),
											  DescribeWorktreeWrite(m_WorktreeChanges.Model().GetWriteReport(), worktreeFolder)),
			};

			messages.Git = GitStatusText(gitStatusInputs);

			workingTreeHasChanges = gitStatus != nullptr && !gitStatus->Entries.empty();
		}

		// WARNING: Before the toasts draw: see MissedForTheBell.
		if (const std::optional<Core::ToastKind> missedKind = m_Notifications.MissedForTheBell())
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
			m_Notifications.OpenFromBell(kStatusBarWindowName);
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
				// WARNING: Both bars push everything below them down, so each appears or goes only while the pointer is
				// settled: a bar that came between the two clicks of a double click moved the second onto another row.
				const bool pointerSettled = IsPointerSettled();
				const bool restrictedWanted = m_Workspace.IsOpen() && !workspaceTrusted &&
											  !m_RestrictedBarsHiddenUntilRestart.contains(m_Workspace.GetRoot());

				if (m_RestrictedBar.Update(restrictedWanted ? std::optional<bool>{true} : std::nullopt, pointerSettled))
				{
					RenderRestrictedModeBar();
				}

				if (const std::optional<LanguagePrompt>& shown = m_LanguageBar.Update(languagePrompt, pointerSettled))
				{
					RenderLanguageSupportBar(*shown);
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
		RenderForcePushConfirm();
		m_About.Render();
		m_QuickOpen.Render();
		m_CommandPalette.Render();
		RenderTrustParentConfirm();

		UpdateGitDecorations();

		// WARNING: Panels render outside the host window, because inside it they would dock as ordinary child windows instead of landing in the dockspace nodes.
		m_Panels.OnImGuiRender(m_TabCloseRequests);

		const CommandContext commandContext{.Commands = &m_Commands,
											.Keys = &m_Keys,
											.Router = &m_Router,
											.Typing = &m_Typing,
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
											 .LineAuthorDelayMs = m_EditorSettings.GetLineAuthorDelayMs(),
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

		m_FolderReviews.Render(ReviewLook{.Danger = DangerColourOf(m_ActiveScheme.Get()),
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

		// WARNING: Draw the toasts last, so they sit over every panel and terminal.
		const Core::ToastPalette toastPalette = ToastPaletteFor(m_ActiveScheme.Get());
		m_Notifications.Render(toastPalette, m_Workspace.GetRoot(), m_LocalTimeZone);

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

		// WARNING: A text field being typed into reads its keys from ImGui on its own. Only a shortcut that fires in text fields may act on the stroke as well; the panels and the editor queue must not, as the user is typing into the field.
		if (event.IsInCategory(Core::EventCategory::Keyboard) && ImGui::GetIO().WantTextInput)
		{
			RouteShortcuts(event, Core::TypingTarget::TextField);
			event.Handled = true;
			return;
		}

		// WARNING: Panels see the event before the router: a chord being recorded is consumed there, and capturing Ctrl+Shift+S would otherwise bind it and save everything in the same keystroke.
		m_Panels.OnEvent(event);

		if (event.Handled)
		{
			return;
		}

		RouteShortcuts(event, m_Typing.GetTarget());

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
			m_Workspace.RequestOpenFolder(dropped.Root);
			break;

		case DropTarget::Solution:
			m_Workspace.RequestOpenSolution(dropped.Root);
			break;
		}
	}

	void GraphiteLayer::ReportGitWriteFailure(const GitModel::WriteFailure& failure)
	{
		ShowGitWriteFailure(std::string{WriteFailureTitle(failure.Kind)}, failure, m_Git.GetWorkingDirectory());
	}

	void GraphiteLayer::ReportGitWriteFailure(const WorktreeWriteFailure& failure)
	{
		const std::string folder = Core::ToUtf8(Core::NormalizeDirectory(failure.Worktree).filename());
		ShowGitWriteFailure(WorktreeWriteFailureTitle(failure.Failure.Kind, folder), failure.Failure, failure.Worktree);
	}

	void GraphiteLayer::ShowGitWriteFailure(std::string title, const GitModel::WriteFailure& failure,
											const std::filesystem::path& workingDirectory)
	{
		std::string shown = RelativiseUnder(failure.Error.Message, workingDirectory);
		std::string copied = title + "\n" + failure.Error.Message;

		m_Notifications.Show(ToastEvent{.Kind = Core::ToastKind::Error, .Title = std::move(title), .Message = std::move(shown), .CopyText = std::move(copied), .Scope = ToastScope::Folder},
							 m_Workspace.GetRoot());
	}

	void GraphiteLayer::ReportGitWriteFailure(const NetworkFailureReport& failure)
	{
		std::string shown = RelativiseUnder(failure.Error.Message, m_Git.GetWorkingDirectory());

		m_Notifications.Show(ToastEvent{.Kind = Core::ToastKind::Error, .Title = FailureTitle(failure.Operation), .Message = std::move(shown), .CopyText = failure.Copied, .Scope = ToastScope::Folder},
							 m_Workspace.GetRoot());
	}

	void GraphiteLayer::ApplyNetworkResults()
	{
		bool anyEnded = false;

		while (m_Network.TakeDone().has_value())
		{
			anyEnded = true;
		}

		while (const std::optional<NetworkFailureReport> failure = m_Network.TakeFailure())
		{
			ReportGitWriteFailure(*failure);
			anyEnded = true;
		}

		// WARNING: Said here as well as by the watcher, which never sees `.git/` when the workspace is a subfolder of the
		// repository or a linked worktree. A cancelled or failed fetch may have moved some refs too.
		if (anyEnded)
		{
			m_Events.Publish(RepositoryChangedEvent{});
		}
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

	bool GraphiteLayer::IsOpenFolderTrusted() const
	{
		return m_Workspace.IsOpen() && m_Trust.IsTrusted(m_Workspace.GetRoot());
	}

	void GraphiteLayer::RenderRestrictedModeBar()
	{
		if (!m_Workspace.IsOpen())
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

	std::optional<GraphiteLayer::ForcePushQuestion> GraphiteLayer::DivergedBranch() const
	{
		const Core::Ref<const std::vector<Core::Branch>> branches = m_Git.GetBranches();
		const Core::Branch* const checkedOut = branches != nullptr ? CheckedOutBranch(*branches) : nullptr;

		if (checkedOut == nullptr || !checkedOut->Upstream.has_value() || !checkedOut->DistanceKnown || checkedOut->Ahead == 0 ||
			checkedOut->Behind == 0)
		{
			return std::nullopt;
		}

		return ForcePushQuestion{.Branch = checkedOut->Name,
								 .Upstream = *checkedOut->Upstream,
								 .Ahead = checkedOut->Ahead,
								 .Behind = checkedOut->Behind};
	}

	void GraphiteLayer::RenderForcePushConfirm()
	{
		DialogLayout::BeforeBegin(DialogLayout::kNarrowWidth);
		const ForcePushQuestion* asked = m_ForcePushQuestion.Begin();
		DialogLayout::AfterBegin();

		if (asked == nullptr)
		{
			return;
		}

		const auto commits = [](std::size_t count) { return std::format("{} {}", count, count == 1 ? "commit" : "commits"); };

		DialogLayout::Text(std::format("Replace {} with {}?", asked->Upstream, asked->Branch));
		DialogLayout::Gap();
		DialogLayout::MutedText(std::format(
			"{} has {} that {} lacks. {} dropped from the remote for everyone, as after amending a commit that was "
			"already pushed. The push is refused if the remote moved since the last fetch.",
			asked->Upstream, commits(asked->Behind), asked->Branch, asked->Behind == 1 ? "It is" : "They are"));
		DialogLayout::Gap();

		const DialogLayout::ButtonSpec noSafeChoice{};
		const DialogLayout::Choice choice =
			DialogLayout::ButtonRow(noSafeChoice, {.Label = "Force Push"}, DangerColourOf(m_ActiveScheme.Get()));

		if (choice == DialogLayout::Choice::Destructive)
		{
			m_Network.ForcePush();
			m_ForcePushQuestion.Answer();
		}
		else if (choice == DialogLayout::Choice::Cancel)
		{
			m_ForcePushQuestion.Answer();
		}

		m_ForcePushQuestion.End();
	}

	void GraphiteLayer::RouteShortcuts(Core::Event& event, Core::TypingTarget typingTarget)
	{
		Core::EventDispatcher dispatcher{event};

		dispatcher.Dispatch<Core::KeyPressedEvent>([this, typingTarget](Core::KeyPressedEvent& pressed) {
			return m_Router.OnKeyPressed(m_Commands, m_Keys, Core::ShortcutScope::Global, typingTarget, pressed);
		});

		dispatcher.Dispatch<Core::KeyReleasedEvent>([this, typingTarget](Core::KeyReleasedEvent& released) {
			return m_Router.OnKeyReleased(m_Commands, m_Keys, typingTarget, released);
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

		case ClosePurpose::ReplaceFolder:
			m_ApprovedReplace = std::exchange(m_PendingReplace, {});
			break;
		}
	}

	void GraphiteLayer::AskBeforeReplacingFolder(std::function<void()> open)
	{
		if (!m_DocumentWindows.HasModifiedDocuments())
		{
			open();
			return;
		}

		// WARNING: One close question at a time, as RequestCloseConfirmation keeps it: the one already asked goes on.
		if (m_CloseQuestion.IsAsked())
		{
			return;
		}

		m_PendingReplace = std::move(open);
		m_CloseQuestion.Ask(ClosePurpose::ReplaceFolder);
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

		DialogLayout::Text(purpose == ClosePurpose::ReplaceFolder
							   ? "Opening another folder closes every open file. These have unsaved changes:"
							   : "These files have unsaved changes:");
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
			m_PendingReplace = {};
			m_CloseQuestion.Answer();
		}

		m_CloseQuestion.End();
	}
}
