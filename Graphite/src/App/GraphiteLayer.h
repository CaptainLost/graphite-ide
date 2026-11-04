#pragma once

#include "Core/Base/Base.h"

#include "Editor/Overlays/AnalysisCache.h"
#include "Editor/DocumentBuffers.h"
#include "Editor/DocumentWindows.h"
#include "Editor/EditorCommands.h"
#include "Editor/EditorCommandTarget.h"
#include "Editor/EditorSettings.h"
#include "Editor/Server/LanguageServicesPort.h"
#include "App/AboutDialog.h"
#include "UI/InfoBars/LanguageSupportBar.h"
#include "UI/Dialogs/ModalQuestion.h"
#include "Editor/Server/PreviewSemantics.h"
#include "App/TitleBar.h"
#include "Git/Blame/BlameModel.h"
#include "Git/Changes/ChangeMarkersModel.h"
#include "Git/Changes/ChangesShared.h"
#include "Git/Changes/ChangesView.h"
#include "Git/Diff/DiffWindows.h"
#include "Git/GitDecorations.h"
#include "Git/GitModel.h"
#include "Git/Lines/BranchLines.h"
#include "Git/Lines/LinesModel.h"
#include "Git/Worktrees/WorktreesModel.h"
#include "Git/RepositoryGitPort.h"
#include "Git/WriteConsequences.h"
#include "Hex/HexFiles.h"
#include "Image/ImageTabs.h"
#include "Notifications/NotificationHistory.h"
#include "Notifications/NotificationsPopup.h"
#include "UI/ToastEvent.h"
#include "Language/Analysis/FileTextSource.h"
#include "Language/Analysis/WorkspaceAnalysisCoordinator.h"
#include "Language/Diagnostics/DiagnosticsModel.h"
#include "Language/Install/ServerInstalls.h"
#include "Language/Server/LanguageServices.h"
#include "Sessions/SessionWindows.h"
#include "Terminal/TerminalWindows.h"
#include "Persistence/StateKeeper.h"
#include "Review/ReviewMenu.h"
#include "Review/ReviewModel.h"
#include "Review/ReviewProgress.h"
#include "Review/ReviewStore.h"
#include "Review/ReviewWindows.h"
#include "Review/WorktreeWatch.h"
#include "Search/SearchService.h"
#include "Search/SearchSettings.h"
#include "Sessions/ClaudeSettings.h"
#include "Settings/AnalysisSettings.h"
#include "Settings/AppSettings.h"
#include "Settings/WorkspaceSettings.h"
#include "Theme/ActiveScheme.h"
#include "Settings/UserDirectories.h"
#include "Workspace/DroppedPaths.h"
#include "Workspace/FolderTransitions.h"
#include "Workspace/Workspace.h"
#include "Workspace/WorkspaceTrust.h"

#include "Core/App/Layer.h"
#include "Core/Base/RingBufferSink.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"
#include "Core/Command/MenuBar.h"
#include "Core/Command/ShortcutRouter.h"
#include "Core/Command/TypingClaim.h"
#include "Core/Events/EventBus.h"
#include "Core/Events/FrameInput.h"
#include "Core/ImGui/DockSpaceHost.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/TabCloseRequests.h"
#include "Core/ImGui/Toasts.h"
#include "Core/ImGui/ToolStrip.h"
#include "Core/Jobs/JobQueue.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Time/TimeZone.h"
#include "Core/UI/PanelManager.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Graphite {

	class ExplorerPanel;
	class SearchPanel;
	class SettingsPanel;
	class ReferencesPanel;
	class GitView;
	class SessionsPanel;

	class GraphiteLayer final : public Core::Layer
	{
	public:
		// WARNING: `settings` must outlive the layer, which reads it, changes it and writes it at the end of every frame that changed it.
		GraphiteLayer(Core::Ref<Core::RingBufferSink> sink, std::filesystem::path initialFolder,
					  AppSettings& settings);

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(Core::Timestep timestep) override;
		void OnImGuiRender() override;
		void OnEvent(Core::Event& event) override;

		bool OnCloseRequested() override;

	private:
		enum class ClosePurpose
		{
			ExitApplication,
			CloseWorkspace
		};

		struct DroppedWorkspace
		{
			DropTarget Target = DropTarget::None;
			std::filesystem::path Root;

			// WARNING: Comparable because ModalQuestion refuses to queue a question it is already asking.
			bool operator==(const DroppedWorkspace&) const = default;
		};

		void RegisterApplicationCommands();

		void ReportConflictingKeyBindings();

		void SubscribeToWorkspaceEvents();

		void AddFolderParticipants();

		// WARNING: An idle worker keeps its last folder's packfiles mapped until its next call, which blocks moving
		// or deleting that folder on Windows.
		void LetGoOfOldRepositoryOnWriteWorker();

		// The `now` of the line model: seconds since the epoch by the system clock.
		static std::int64_t SystemClockSeconds();

		void OpenStartupFolder();

		void RouteShortcuts(Core::Event& event, Core::TypingTarget typingTarget);

		void HandleFileDrop(Core::Event& event);

		void OpenDropped(const DroppedWorkspace& dropped);

		void RenderDropConfirm();

		void RenderRestrictedModeBar(bool trusted);

		void RenderLanguageSupportBar(const LanguagePrompt& prompt);

		void RenderTrustParentConfirm();

		void RenderEmptyEditorArea();

		void ShowToast(Core::ToastKind kind, std::string title, std::string message, std::string copyText, ToastScope scope);

		void ReportGitWriteFailure(const GitModel::WriteFailure& failure);

		void QueueForFocusedEditor(Core::Event& event);

		void RequestCloseConfirmation(ClosePurpose purpose);

		void FinishClose(ClosePurpose purpose);

		void RenderCloseConfirm();

		bool ConfirmationStillNeeded(ClosePurpose purpose);

		void ListModifiedDocuments() const;

		bool IsDocumentOpen(const std::filesystem::path& path) const;

		bool IsDocumentShown(const std::filesystem::path& path) const;


		void UpdateFocusReload(Core::Timestep timestep);

		void ApplyChromeIfSchemeChanged();

		// WARNING: Call before the panels render, because the Explorer is handed this frame's view of the decorations.
		void UpdateGitDecorations();

		void RevealPanel(std::string_view id);

		void OpenLanguageSettings(LanguageId language);

		Core::ToolStripContent BuildToolStrip(bool workingTreeHasChanges, const DiagnosticsModel::Counts& problems,
											  const Core::ChromePalette& chrome);

		void ApplyToolStripClick(std::string_view id);

		void PublishServerMessages();

		static Core::Ref<Core::HttpClient> CreateInstallClient();
		void ApplyFinishedInstalls();
		bool CanInstallChosenServerFor(const std::filesystem::path& path);
		void InstallChosenServerFor(LanguageId language);

		GlobalState CaptureGlobalState();

		void FollowBranchesIntoReviews();

		ReviewMenu ReviewMenuOfBranch(const Core::Branch& branch) const;

		// Takes what the review bases and the own-authors walk need from a line read that has just landed.
		void FollowLinesIntoReviewsAndAuthors();
		void ApplyReviewMenuItem(const ReviewMenuItem& item);

		// Opens a draft against ReviewBaseOf's base for a branch with neither a draft nor a session. False while the
		// branch still has neither: the branches are unread, or there is nothing to review it against.
		bool OpenReviewDraft(const std::string& branch);

		// The tab exists only with a kept session, so the draft is kept first.
		void OpenReviewTab(const std::string& branch);

		ReviewRenderInput ReviewInputWith(const GitPalette& palette, const SyntaxPalette& syntax);

		static constexpr float kReloadAfterSecondsAway = 2.0f;

		bool m_CustomTitleBar = false;

		bool m_AppForeground = true;
		float m_SecondsInBackground = 0.0f;

		bool m_CloseApproved = false;

		Core::Ref<Core::RingBufferSink> m_Sink;
		std::filesystem::path m_InitialFolder;

		// WARNING: Declared before every member below that holds it, because they are all constructed from this reference.
		AppSettings& m_AppSettings;

		Core::EventBus m_Events;

		Core::FrameInput m_FrameInput;

		// WARNING: m_Jobs is declared below; the workspace submits only once attached, and its jobs hold nothing of it.
		Workspace m_Workspace{m_Events, m_AppSettings, Core::DirectoryWatcher::Start,
							  [this](std::function<void()> job) { m_Jobs.Submit(std::move(job)); }};
		Core::DocumentSet m_Documents;

		// WARNING: Declared before m_LanguageServices, whose root permission reads it, so it is destroyed after.
		WorkspaceTrust m_Trust{UserDirectories::TrustedFoldersFile()};

		// WARNING: Its participants are added in OnAttach, once the Git window exists; they hold references only and
		// do nothing on destruction, so the order of the members they refer to does not matter here.
		FolderTransitions m_Folder{m_Trust, [](const std::filesystem::path& root) { return WorkspaceSettings{root}.LoadState(); }};

		// WARNING: Declared after m_Workspace, because its fallback root reads m_Workspace's;
		// member initialisers run in declaration order regardless of the order they are listed in.
		LanguageServices m_LanguageServices{m_Workspace.GetRoot()};

		// WARNING: Declared before m_ServerInstalls so it is destroyed after it: the installer cancels its jobs, then this joins them.
		Core::JobQueue m_InstallJobs{1};
		ServerInstalls m_ServerInstalls{CreateInstallClient(), UserDirectories::ServersDirectory(),
										[this](std::function<void()> job) { m_InstallJobs.Submit(std::move(job)); }};

		std::set<LanguageId> m_LanguagePromptsDismissedUntilRestart;
		std::set<std::filesystem::path> m_RestrictedBarsHiddenUntilRestart;

		// WARNING: Declared after m_LanguageServices, which fills it each frame, and before
		// m_DocumentWindows and m_Panels, which hold it by reference and read it through teardown.
		DiagnosticsModel m_Diagnostics;

		ModalGate m_Modals;

		// WARNING: Declared after m_LanguageServices, which it sends to, and before the three ports below: the preview and
		// the analysis let go of their documents from their destructors, so the holders must outlive them.
		LanguageServicesHeldPort m_HeldDocumentPort{m_LanguageServices};
		DocumentHolders m_DocumentHolders{m_HeldDocumentPort};

		// WARNING: Declared after m_LanguageServices, which the port forwards to, and before
		// m_DocumentWindows and m_Panels, which hold the preview members by reference.
		LanguageServicesPort m_PreviewPort{m_LanguageServices, m_DocumentHolders};
		PreviewSemantics m_PreviewSemantics{m_PreviewPort};

		// WARNING: Declared after m_LanguageServices, which it forwards to, and before m_DocumentWindows, which holds it by reference.
		DocumentServicesPort m_DocumentPort{m_LanguageServices, m_DocumentHolders};

		// WARNING: Declared before m_Panels, which owns the background analysis page that reads it.
		AnalysisSettings m_AnalysisSettings{m_AppSettings};

		// WARNING: Declared ahead of both its producers, m_AnalysisCoordinator and m_DocumentWindows,
		// so that neither is left holding a reference to a destroyed cache.
		AnalysisCache m_Analysis{AnalysisSettings::kDefaultBudgetMegabytes * AnalysisSettings::kBytesPerMegabyte};

		// WARNING: Declared before the coordinator whose pass asks it for text, so the requests stop arriving before the thread is joined.
		DiskFileTexts m_AnalysisTexts;

		// WARNING: Declared after m_LanguageServices, which both ports forward to, and before
		// m_AnalysisCoordinator: the pass closes its open documents from its own destructor,
		// so the ports and the servers behind them must both still be there.
		WorkspaceAnalysisServicesPort m_AnalysisPort{m_LanguageServices, m_DocumentHolders};
		LanguageServicesWorkspacePort m_AnalysisServers{m_LanguageServices};

		// WARNING: Declared after everything it holds a reference to (the ports, m_AnalysisTexts, m_Analysis)
		// and before m_DocumentWindows, which holds its store and its pass by reference and reads both whenever
		// a tab opens, so neither reference can dangle.
		WorkspaceAnalysisCoordinator m_AnalysisCoordinator{m_AnalysisServers, m_AnalysisPort,
														   m_AnalysisTexts, m_Analysis};

		Core::TabCloseRequests m_TabCloseRequests;

		// WARNING: Declared before m_DocumentWindows, which holds it by reference and sets itself on it.
		ActiveEditorTarget m_ActiveEditor;

		// WARNING: Declared before m_DocumentWindows, which holds it by reference. Its jobs hold only their own
		// mailboxes, so it may outlive m_Jobs, declared further down; its textures go before the renderer's factory,
		// which outlives every layer.
		ImageTabs m_ImageTabs{[this](std::function<void()> job) { m_Jobs.Submit(std::move(job)); },
							  Core::ImGuiRenderer::GetTextures()};

		// WARNING: Declared before m_DocumentWindows, which holds it by reference. Its reads hold only their own
		// mailboxes, so it may outlive m_Jobs, declared further down.
		HexFiles m_HexFiles{[this](std::function<void()> job) { m_Jobs.Submit(std::move(job)); }};

		// WARNING: Declared after m_DocumentPort, m_Analysis and m_AnalysisCoordinator, which it holds by reference.
		// m_ChangeMarkers and m_Blame are declared further down and are not yet constructed here; only references
		// to them are bound, and nothing reaches through those references until the first frame.
		DocumentWindows m_DocumentWindows{m_Documents, m_Modals, m_DocumentPort,
										  m_Diagnostics, m_ChangeMarkers, m_Blame, m_PreviewSemantics,
										  m_Analysis, m_AnalysisCoordinator.GetStore(),
										  m_AnalysisCoordinator.GetPass(), m_Events, m_ActiveEditor,
										  m_ImageTabs, m_HexFiles};

		// WARNING: Declared after m_Documents and m_DocumentWindows and before m_Panels, where GitView holds it by reference.
		DocumentBuffers m_DocumentBuffers{m_Documents, m_DocumentWindows};

		// WARNING: Declared before m_Search, so it is destroyed after it: the service cancels its running search
		// first, and this queue then joins a worker that is already stopping rather than one still scanning.
		Core::JobQueue m_SearchJobs;

		// WARNING: Declared before m_Panels, because SearchPanel holds references to m_Workspace, m_Documents,
		// m_Search and m_SearchSettings, and a member declared after m_Panels is destroyed first.
		SearchService m_Search{[this](std::function<void()> job) { m_SearchJobs.Submit(std::move(job)); }};
		SearchSettings m_SearchSettings{m_AppSettings};

		// WARNING: Declared before m_Panels, because FindInFilesPage holds a reference to it.
		bool m_SolutionLoaded = false;

		// WARNING: Declared before m_Panels, because SearchPanel holds a reference to each
		// and a member declared after m_Panels is destroyed while the panel still holds it.
		EditorSettings m_EditorSettings{m_AppSettings};
		ActiveScheme m_ActiveScheme;

		static constexpr std::uint32_t kSchemeNeverApplied = 0;

		// WARNING: ActiveScheme versions start at one, so the first frame always applies the scheme.
		std::uint32_t m_AppliedSchemeVersion = kSchemeNeverApplied;

		static constexpr const char* kUnsavedWorkTitle = "Unsaved changes##ExitUnsavedWork";

		ModalQuestion<ClosePurpose> m_CloseQuestion;

		static constexpr const char* kReplaceWorkspaceTitle =
			"Open dropped folder##DropReplacesWorkspace";

		ModalQuestion<DroppedWorkspace> m_DropQuestion;

		AboutDialog m_About;

		static constexpr const char* kTrustParentTitle = "Trust all folders##TrustParentFolders";

		ModalQuestion<std::filesystem::path> m_TrustParentQuestion;

		// WARNING: Declared before m_Panels, which constructs the git views that hold references to these.
		// The order among these nine is deliberate. Every job that GitModel, ChangeMarkersModel, BlameModel,
		// ReviewModel, LinesModel and WorktreesModel submit captures `this` and reads m_GitPort, so the two queues are declared LAST
		// of the nine: members are destroyed in reverse order, which joins every worker before the models and the port
		// go away. Declaring a queue first compiles and fails only under real threading.
		RepositoryGitPort m_GitPort{std::filesystem::path{}};
		GitModel m_Git{m_GitPort, [this](std::function<void()> job) { m_Jobs.Submit(std::move(job)); },
					   [this](std::function<void()> job) { m_GitWrites.Submit(std::move(job)); }};
		ChangeMarkersModel m_ChangeMarkers{
			m_GitPort, [this](std::function<void()> job) { m_Jobs.Submit(std::move(job)); }};
		BlameModel m_Blame{m_GitPort, [this](std::function<void()> job) { m_Jobs.Submit(std::move(job)); }};
		ReviewModel m_Reviews{m_GitPort, [this](std::function<void()> job) { m_Jobs.Submit(std::move(job)); },
							  WatchWithDirectoryWatcher()};
		LinesModel m_Lines{m_GitPort, [this](std::function<void()> job) { m_Jobs.Submit(std::move(job)); }, &SystemClockSeconds};
		WorktreesModel m_Worktrees{m_GitPort, [this](std::function<void()> job) { m_Jobs.Submit(std::move(job)); }};
		Core::JobQueue m_Jobs;

		static constexpr std::size_t kGitWriteWorkers = 1;
		Core::JobQueue m_GitWrites{kGitWriteWorkers};

		// WARNING: m_Events is destroyed after m_Git although m_Git subscribes handlers into it that capture `this`.
		// That is unreachable today, because nothing publishes from a destructor and EventBus has no unsubscribe;
		// a member declared between m_Events and the group above must not change that.

		// WARNING: Declared before m_Panels, because the Explorer holds a pointer into m_Decorations between rebuilds.
		Core::Ref<const Core::WorkingTreeStatus> m_DecoratedStatus;
		std::unique_ptr<GitDecorations> m_Decorations;
		std::uint64_t m_DecorationsVersion = 0;

		// WARNING: Declared before m_Panels, where GitView, which takes a DiffWindows&, is constructed.
		DiffWindows m_DiffWindows;

		// WARNING: Declared before m_Panels, because ChangesPanel holds it by reference.
		ChangesShared m_ChangesShared;

		// WARNING: Declared after m_ChangesShared, which it holds, and before m_Panels, where GitView holds it by
		// reference. The Git window's host of the Changes view; the side panel owns its own.
		ChangesView m_GitWindowChanges{m_Git, m_Modals, m_DocumentBuffers, m_ActiveScheme, m_Events, m_ChangesShared,
									   "window"};

		// WARNING: Declared after m_Git, m_DocumentBuffers, m_ChangeMarkers, m_Blame and m_ChangesShared, which it holds.
		WriteConsequences m_WriteConsequences{m_Git, m_DocumentBuffers, m_ChangesShared,
											  [this](const GitModel::WriteFailure& failure) { ReportGitWriteFailure(failure); },
											  [this] {
												  m_ChangeMarkers.Invalidate();
												  m_Blame.Invalidate();
											  }};

		ReviewWindows m_ReviewWindows{m_Modals};
		ReviewStore m_ReviewStore;

		Core::Ref<const std::vector<Core::Branch>> m_ReviewedBranches;

		// WARNING: m_ReviewLines views into m_ReviewLinesRead, which it keeps alive; the two are replaced together.
		Core::Ref<const LineRead> m_ReviewLinesRead;
		std::optional<BranchLines> m_ReviewLines;

		// WARNING: Declared before m_Panels, because SessionsPanel holds a reference to each and ClaudeSessionsPage to the settings.
		// m_SessionWindows joins a worker per open session in its own destructor, and nothing it refers to is declared after it.
		ClaudeSettings m_ClaudeSettings{m_AppSettings};
		SessionWindows m_SessionWindows{m_Events, m_ClaudeSettings};

		// WARNING: Declared before m_Panels, because GitView holds a reference to m_Toasts.
		Core::Toasts m_Toasts;
		NotificationHistory m_Notifications;
		NotificationsPopup m_NotificationsPopup;
		const std::chrono::time_zone* m_LocalTimeZone = Core::LocalTimeZone();

		Core::PanelManager m_Panels;
		Core::DockSpaceHost m_DockSpace;
		Core::DockLayout m_Layout;
		TitleBar m_TitleBar;

		Core::Keymap m_Keys;

		// Claimed by the editor or the terminal holding the keyboard as it draws, read as the next strokes arrive.
		Core::TypingClaim m_Typing;

		// WARNING: Declared after m_Keys, m_Workspace and m_Typing, which it refers to; it joins a reader thread per terminal in its destructor.
		TerminalWindows m_Terminals{m_Workspace, m_Keys, m_Commands, m_AppSettings, m_Events, m_Typing};

		// WARNING: Declared after every member whose commands it holds, because each command captures its owner,
		// so it is destroyed before any of them. Nothing reads it from a member initialiser;
		// commands are registered in the constructor's body, once every member exists.
		Core::CommandRegistry m_Commands;
		Core::ShortcutRouter m_Router;
		Core::MenuBar m_MenuBar;

		std::vector<Core::MenuItem> m_MenuTree;

		std::vector<Core::MenuItem> m_EditorContextMenu = BuildEditorContextMenu();

		ExplorerPanel* m_Explorer = nullptr;
		SearchPanel* m_SearchPanel = nullptr;
		ReferencesPanel* m_ReferencesPanel = nullptr;
		SettingsPanel* m_SettingsPanel = nullptr;
		GitView* m_GitView = nullptr;
		SessionsPanel* m_SessionsPanel = nullptr;

		StateKeeper m_StateKeeper{
			[this](const GlobalState& state) { m_AppSettings.SetGlobalState(state); },
			[](const std::filesystem::path& root, const WorkspaceState& state) {
				WorkspaceSettings{root}.SaveState(state);
			}};
	};
}
