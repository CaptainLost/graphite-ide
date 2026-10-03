#pragma once

#include "Core/Base/Base.h"

#include "Editor/AnalysisCache.h"
#include "Editor/DocumentBuffers.h"
#include "Editor/DocumentWindows.h"
#include "Editor/EditorCommands.h"
#include "Editor/EditorSettings.h"
#include "Editor/LanguageServicesPort.h"
#include "Editor/ModalQuestion.h"
#include "Editor/PreviewSemantics.h"
#include "App/TitleBar.h"
#include "Git/ChangeMarkersModel.h"
#include "Git/DiffWindows.h"
#include "Git/GitModel.h"
#include "Git/RepositoryGitPort.h"
#include "Language/Analysis/FileTextSource.h"
#include "Language/Analysis/WorkspaceAnalysisCoordinator.h"
#include "Language/Diagnostics/DiagnosticsModel.h"
#include "Language/Server/LanguageServices.h"
#include "Sessions/SessionWindows.h"
#include "Terminal/TerminalWindows.h"
#include "Persistence/StateKeeper.h"
#include "Search/SearchService.h"
#include "Search/SearchSettings.h"
#include "Sessions/ClaudeSettings.h"
#include "Settings/AnalysisSettings.h"
#include "Settings/AppSettings.h"
#include "Settings/WorkspaceSettings.h"
#include "Theme/ActiveScheme.h"
#include "Settings/UserDirectories.h"
#include "Workspace/DroppedPaths.h"
#include "Workspace/Workspace.h"
#include "Workspace/WorkspaceTrust.h"

#include "Core/App/Layer.h"
#include "Core/Base/RingBufferSink.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"
#include "Core/Command/MenuBar.h"
#include "Core/Command/ShortcutRouter.h"
#include "Core/Events/EventBus.h"
#include "Core/Events/FrameInput.h"
#include "Core/ImGui/DockSpaceHost.h"
#include "Core/ImGui/TabCloseRequests.h"
#include "Core/Jobs/JobQueue.h"
#include "Core/Text/DocumentSet.h"
#include "Core/UI/PanelManager.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace Graphite {

	class SearchPanel;
	class ReferencesPanel;

	class GraphiteLayer final : public Core::Layer
	{
	public:
		// `settings` is the application's, and outlives the layer: the layer reads and changes it, and
		// writes it at the end of every frame that changed it.
		GraphiteLayer(Core::Ref<Core::RingBufferSink> sink, std::filesystem::path initialFolder,
					  AppSettings& settings);

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(Core::Timestep ts) override;
		void OnImGuiRender() override;
		void OnEvent(Core::Event& event) override;

		bool OnCloseRequested() override;

	private:
		enum class ClosePurpose
		{
			ExitApplication,
			CloseWorkspace
		};

		// The part of a drop that replaces the workspace, kept while the user is asked
		// whether it may. Comparable because ModalQuestion refuses to queue a question it is
		// already asking, and dropping the same folder twice is one question.
		struct DroppedWorkspace
		{
			DropTarget Target = DropTarget::None;
			std::filesystem::path Root;

			bool operator==(const DroppedWorkspace&) const = default;
		};

		void RegisterApplicationCommands();

		void ReportConflictingKeyBindings();

		void SubscribeToWorkspaceEvents();

		void OpenStartupFolder();

		void RouteShortcuts(Core::Event& event);

		void HandleFileDrop(Core::Event& event);

		void OpenDropped(const DroppedWorkspace& dropped);

		void RenderDropConfirm();

		void RenderRestrictedModeBar(bool trusted);

		void RenderEmptyEditorArea();

		void QueueForFocusedEditor(Core::Event& event);

		void RequestCloseConfirmation(ClosePurpose purpose);

		void FinishClose(ClosePurpose purpose);

		void RenderCloseConfirm();

		bool ConfirmationStillNeeded(ClosePurpose purpose);

		void ListModifiedDocuments() const;

		ClosePurpose AskedPurposeOrExit() const;

		void UpdateFocusReload(Core::Timestep ts);

		void ApplyChromeIfSchemeChanged();

		void RevealPanel(std::string_view id);

		GlobalState CaptureGlobalState();

		// The open folder's file tabs, session windows and diff windows, as one snapshot.
		WorkspaceState CaptureWorkspaceState() const;

		static constexpr float kReloadAfterSecondsAway = 2.0f;

		bool m_CustomTitleBar = false;

		bool m_AppForeground = true;
		float m_SecondsInBackground = 0.0f;

		bool m_CloseApproved = false;

		Core::Ref<Core::RingBufferSink> m_Sink;
		std::filesystem::path m_InitialFolder;

		// Before every member below that holds it: they are all constructed from this reference.
		AppSettings& m_AppSettings;

		Core::EventBus m_Events;

		// Keyboard input that reached neither a panel nor the shortcut router, kept for the
		// editors to read while they draw.
		Core::FrameInput m_FrameInput;

		Workspace m_Workspace{m_Events, m_AppSettings};
		Core::DocumentSet m_Documents;

		// Before m_LanguageServices, whose root permission reads it, so it is destroyed after.
		WorkspaceTrust m_Trust{UserDirectories::TrustedFoldersFile()};

		// Declared after m_Workspace so its fallback root can read m_Workspace's — member
		// initialisers run in declaration order regardless of the order they are listed in.
		// Owns every language server the editor talks to; the status bar reads its state each
		// frame through StatusFor.
		LanguageServices m_LanguageServices{m_Workspace.GetRoot()};

		// Declared after m_LanguageServices, which fills it each frame in OnImGuiRender, and
		// before m_DocumentWindows and m_Panels, both of which hold this by reference: the model
		// must outlive every reader of it through teardown.
		DiagnosticsModel m_Diagnostics;

		ModalGate m_Modals;

		// Declared after m_LanguageServices, which the port forwards to, and before
		// m_DocumentWindows and m_Panels, both of which hold this by reference.
		LanguageServicesPort m_PreviewPort{m_LanguageServices};
		PreviewSemantics m_PreviewSemantics{m_PreviewPort};

		// What m_DocumentWindows asks the servers through. Declared after m_LanguageServices,
		// which it forwards to, and before m_DocumentWindows, which holds it by reference.
		DocumentServicesPort m_DocumentPort{m_LanguageServices};

		// Read by LanguageSection, which m_Panels owns, so this is declared before m_Panels for
		// the same reason m_EditorSettings is.
		AnalysisSettings m_AnalysisSettings{m_AppSettings};

		// Ahead of both its producers, so it outlives them: m_AnalysisCoordinator's pass writes
		// entries into it and m_DocumentWindows reads them back when a tab opens, and either
		// holding a reference to a destroyed cache is the failure this order prevents.
		AnalysisCache m_Analysis{AnalysisSettings::kDefaultBudgetMegabytes * 1024ull * 1024ull};

		// The reader's worker, declared before the coordinator whose pass asks it for text, so the
		// requests stop arriving before the thread is joined.
		DiskFileTexts m_AnalysisTexts;

		// Declared after m_LanguageServices, which both ports forward to, and before
		// m_AnalysisCoordinator, which holds them: the pass closes whatever it had open from its
		// own destructor, so the ports and the servers behind them must both still be there.
		WorkspaceAnalysisServicesPort m_AnalysisPort{m_LanguageServices};
		LanguageServicesWorkspacePort m_AnalysisServers{m_LanguageServices};

		// Owns the background analysis - its store, its pass and its walk, in that order inside
		// it - and notices the folder, its trust and the setting changing. After everything it
		// holds a reference to (the ports and the reader just above, m_Analysis further up), so
		// all of them outlive it. Before m_DocumentWindows, which holds its store and its pass by
		// reference and reads both back whenever a tab opens: declared first, it is destroyed
		// last, so neither reference can dangle. Inside it the walk is destroyed first, joining
		// its thread before the pass it feeds goes away, and the store last, after the pass that
		// writes into it.
		WorkspaceAnalysisCoordinator m_AnalysisCoordinator{m_AnalysisServers, m_AnalysisPort,
														   m_AnalysisTexts, m_Analysis};

		// Tabs a context menu asked to close, taken by their owners on the next frame.
		Core::TabCloseRequests m_TabCloseRequests;

		// Declared after m_DocumentPort, which it holds by reference: it opens each document
		// with the matching server and feeds the reference-count rows from the answers. After
		// m_Analysis and m_AnalysisCoordinator too, whose cache, store and pass it holds by
		// reference.
		//
		// m_ChangeMarkers is declared further down, with the git group, and so is not yet
		// constructed here. That is fine: only a reference to it is bound, and nothing reaches
		// through that reference until the first frame.
		DocumentWindows m_DocumentWindows{m_Documents, m_Modals, m_DocumentPort,
										  m_Diagnostics, m_ChangeMarkers, m_PreviewSemantics,
										  m_Analysis, m_AnalysisCoordinator.GetStore(),
										  m_AnalysisCoordinator.GetPass()};

		// The Git panel's view of the editor, over the two members above. Declared after both
		// and before m_Panels, where GitView holds it by reference.
		DocumentBuffers m_DocumentBuffers{m_Documents, m_DocumentWindows};

		// Declared before m_Panels, like m_Workspace and m_Documents above: SearchPanel holds
		// references to both, so both must outlive it through teardown.
		SearchService m_Search;
		SearchSettings m_SearchSettings{m_AppSettings};

		// Declared before m_Panels for the same reason as m_Search: SearchSection holds a
		// reference to this, and a member declared after m_Panels would be destroyed first.
		// Read by SearchSection to grey out "projects only" when there is nothing to limit to,
		// and updated every frame rather than reached for through Workspace: a section knows
		// nothing about Workspace, by design.
		bool m_SolutionLoaded = false;

		// Declared before m_Panels for the same reason as m_Search: SearchPanel holds a
		// reference to each — it previews a file in the editor's font, at the editor's tab
		// width, in the editor's colours — and a member declared after m_Panels would be
		// destroyed while the panel still held it.
		EditorSettings m_EditorSettings{m_AppSettings};
		ActiveScheme m_ActiveScheme;

		// The scheme version the chrome was last painted from. Zero is "never", and
		// ActiveScheme starts at one, so the first frame always applies.
		std::uint32_t m_AppliedSchemeVersion = 0;

		static constexpr const char* kUnsavedWorkTitle = "Unsaved changes##ExitUnsavedWork";

		ModalQuestion<ClosePurpose> m_CloseQuestion;

		static constexpr const char* kReplaceWorkspaceTitle =
			"Open dropped folder##DropReplacesWorkspace";

		ModalQuestion<DroppedWorkspace> m_DropQuestion;

		// Declared before m_Panels for the same reason as m_Search: the git views will hold
		// references to these.
		//
		// The order among these four is not the declaration order the naive reading suggests.
		// Every job GitModel submits captures `this`, so the queue behind the submitter must
		// be destroyed - and its workers joined - before the model goes away, and the port
		// must outlive every query a worker might still be running against it. Members are
		// destroyed in reverse declaration order, so m_Jobs is declared LAST of the four: its
		// destructor runs FIRST, joining every worker before m_Git, m_ChangeMarkers or m_GitPort
		// can be torn down. Declaring m_Jobs first would compile without complaint and fail only under
		// real threading, when a worker is mid-query as the model it points into disappears.
		// m_ChangeMarkers sits with m_Git before m_Jobs for the same reason: every job it submits
		// captures `this` and reads the port as a ChangeBasePort.
		//
		// m_Events, declared far above, is a related trap: it is destroyed AFTER m_Git even
		// though m_Git subscribes handlers into it that capture `this`. That is not reachable
		// today - nothing publishes from a destructor and EventBus has no unsubscribe - but a
		// member declared between m_Events and this group must not change that, so it is
		// recorded here rather than left for the next reader to rediscover.
		RepositoryGitPort m_GitPort{std::filesystem::path{}};
		GitModel m_Git{m_GitPort,
					   [this](std::function<void()> job) { m_Jobs.Submit(std::move(job)); }};
		ChangeMarkersModel m_ChangeMarkers{
			m_GitPort, [this](std::function<void()> job) { m_Jobs.Submit(std::move(job)); }};
		Core::JobQueue m_Jobs;

		// GitModel's applied-write count as OnUpdate last saw it. A change means this process
		// moved HEAD or the index itself, and the change markers are told directly rather than
		// left to the file watcher.
		std::uint64_t m_SeenGitWrites = 0;

		// Holds no reference to m_Git or the queue above, so its place in this group is only
		// about outliving what refers to it: GitView takes a DiffWindows&, so this must be
		// declared before m_Panels, which is where GitView is constructed.
		DiffWindows m_DiffWindows;

		// Before m_Panels: SessionsPanel holds a reference to each, and ClaudeSection to the
		// settings. m_SessionWindows owns a worker per open session; those join in its own
		// destructor, and nothing it refers to is declared after it.
		ClaudeSettings m_ClaudeSettings{m_AppSettings};
		SessionWindows m_SessionWindows{m_Events, m_ClaudeSettings};

		Core::PanelManager m_Panels;
		Core::DockSpaceHost m_DockSpace;
		Core::DockLayout m_Layout;
		TitleBar m_TitleBar;

		Core::Keymap m_Keys;

		// After m_Keys and m_Workspace, which it refers to. Owns a reader thread per terminal;
		// they join in its destructor.
		TerminalWindows m_Terminals{m_Workspace, m_Keys};

		// After every member whose commands it holds - each captures its owner, `this` or a
		// panel - so it is destroyed before any of them and no stored command outlives what it
		// points into. Nothing reads it from a member initialiser; commands are registered in
		// the constructor's body, once every member exists.
		Core::CommandRegistry m_Commands;
		Core::ShortcutRouter m_Router;
		Core::MenuBar m_MenuBar;

		std::vector<Core::MenuItem> m_MenuTree;

		// The text editors' right-click menu. Built once and handed to every editor through
		// CommandContext, so the widget draws commands without naming any.
		std::vector<Core::MenuItem> m_EditorContextMenu = BuildEditorContextMenu();

		SearchPanel* m_SearchPanel = nullptr;
		ReferencesPanel* m_ReferencesPanel = nullptr;

		StateKeeper m_StateKeeper{
			[this](const GlobalState& state) { m_AppSettings.SetGlobalState(state); },
			[](const std::filesystem::path& root, const WorkspaceState& state) {
				WorkspaceSettings{root}.SaveState(state);
			}};
	};
}