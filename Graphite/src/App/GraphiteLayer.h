#pragma once

#include "Core/Base/Base.h"

#include "Editor/AnalysisCache.h"
#include "Editor/DocumentWindows.h"
#include "Editor/EditorSettings.h"
#include "Editor/LanguageServicesPort.h"
#include "Editor/ModalQuestion.h"
#include "Editor/PreviewSemantics.h"
#include "App/TitleBar.h"
#include "Git/ChangeMarkersModel.h"
#include "Git/DiffWindows.h"
#include "Git/GitModel.h"
#include "Git/RepositoryGitPort.h"
#include "Lang/Analysis/AnalysisStore.h"
#include "Lang/Analysis/AnalysisWalk.h"
#include "Lang/Analysis/FileTextSource.h"
#include "Lang/Analysis/WorkspaceAnalysis.h"
#include "Lang/Diagnostics/DiagnosticsModel.h"
#include "Lang/Server/LanguageServices.h"
#include "Panels/SessionWindows.h"
#include "Persistence/StateKeeper.h"
#include "Search/SearchService.h"
#include "Search/SearchSettings.h"
#include "Sessions/ClaudeSettings.h"
#include "Settings/AnalysisSettings.h"
#include "Settings/AppSettings.h"
#include "Settings/WorkspaceSettings.h"
#include "Theme/ActiveScheme.h"
#include "Workspace/DroppedPaths.h"
#include "Workspace/Workspace.h"

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
#include <memory>
#include <vector>

namespace Graphite {

	class SearchPanel;
	class ReferencesPanel;

	class GraphiteLayer final : public Core::Layer
	{
	public:
		GraphiteLayer(Core::Ref<Core::RingBufferSink> sink, std::filesystem::path initialFolder);

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
		Core::EventBus m_Events;

		// Keyboard input that reached neither a panel nor the shortcut router, kept for the
		// editors to read while they draw.
		Core::FrameInput m_FrameInput;

		Workspace m_Workspace{m_Events};
		Core::DocumentSet m_Documents;

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

		// Read by LanguageSection, which m_Panels owns, so this is declared before m_Panels for
		// the same reason m_EditorSettings is.
		AnalysisSettings m_AnalysisSettings;

		// Ahead of both its producers, so it outlives them: m_WorkspaceAnalysis writes entries
		// into it and m_DocumentWindows reads them back when a tab opens, and either holding a
		// reference to a destroyed cache is the failure this order prevents.
		AnalysisCache m_Analysis{AnalysisSettings::kDefaultBudgetMegabytes * 1024ull * 1024ull};

		// Beside the cache it is the second tier of, and ahead of the same two readers for the same
		// reason: m_WorkspaceAnalysis holds it as a pointer and writes every finished file into it,
		// and m_DocumentWindows holds it by reference and reads one back whenever a tab opens. Both
		// are declared below, so both are destroyed first and neither can outlive what it points
		// at. Its own worker is joined by its destructor, which is why nothing above it has to wait
		// for the disk.
		AnalysisStore m_AnalysisStore;

		// The reader's worker, declared before the class that asks it for text so the requests
		// stop arriving before the thread is joined.
		DiskFileTexts m_AnalysisTexts;

		// Declared after m_LanguageServices, which the port forwards to, and before
		// m_WorkspaceAnalysis, which holds the port: the pass closes whatever it had open from its
		// own destructor, so the port and the servers behind it must both still be there.
		WorkspaceAnalysisServicesPort m_AnalysisPort{m_LanguageServices};
		// Both tiers, and both declared above so both outlive this: the pass writes a finished file
		// into the cache and the store together, and it goes on writing until its own destructor
		// has run.
		WorkspaceAnalysis m_WorkspaceAnalysis{m_AnalysisPort, m_AnalysisTexts, m_Analysis,
											  &m_AnalysisStore};

		// Last of this group, so its destructor runs first and the walking thread is cancelled and
		// joined before anything it feeds goes away. It holds no reference to the members above -
		// its findings are drained on the UI thread - so this is the cheap end of the order.
		AnalysisWalk m_AnalysisWalk;

		// Tabs a context menu asked to close, taken by their owners on the next frame.
		Core::TabCloseRequests m_TabCloseRequests;

		// Declared after m_LanguageServices, which it holds by reference: it opens each document
		// with the matching server and feeds the reference-count rows from the answers. After
		// m_Analysis, m_AnalysisStore and m_WorkspaceAnalysis too, all three of which it now holds
		// by reference.
		//
		// m_ChangeMarkers is declared further down, with the git group, and so is not yet
		// constructed here. That is fine: only a reference to it is bound, and nothing reaches
		// through that reference until the first frame.
		DocumentWindows m_DocumentWindows{m_Documents,     m_Modals,   m_LanguageServices,
										  m_Diagnostics,   m_ChangeMarkers, m_PreviewSemantics,
										  m_Analysis,      m_AnalysisStore, m_WorkspaceAnalysis};

		// Declared before m_Panels, like m_Workspace and m_Documents above: SearchPanel holds
		// references to both, so both must outlive it through teardown.
		SearchService m_Search;
		SearchSettings m_SearchSettings;

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
		EditorSettings m_EditorSettings;
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
		ClaudeSettings m_ClaudeSettings;
		SessionWindows m_SessionWindows{m_Events, m_ClaudeSettings};

		Core::PanelManager m_Panels;
		Core::DockSpaceHost m_DockSpace;
		Core::DockLayout m_Layout;
		TitleBar m_TitleBar;

		Core::CommandRegistry m_Commands;
		Core::Keymap m_Keys;
		Core::ShortcutRouter m_Router;
		Core::MenuBar m_MenuBar;

		std::vector<Core::MenuItem> m_MenuTree;

		SearchPanel* m_SearchPanel = nullptr;
		ReferencesPanel* m_ReferencesPanel = nullptr;

		// The root the language servers were last warmed for, so the comparison in OnImGuiRender
		// fires once per folder rather than once per frame.
		std::filesystem::path m_WarmedRoot;

		// What the background-analysis setting was last frame, so switching it back on can restart
		// the walk. Remembered rather than hooked onto the settings section for the reason
		// m_WarmedRoot is remembered rather than hooked onto the places a folder opens — see
		// OnImGuiRender. Starts at AnalysisSettings' own default, so a session that loads that
		// default sees no edge at all on its first frame.
		bool m_AnalysisWasEnabled = true;

		StateKeeper m_StateKeeper{
			[](const GlobalState& state) { AppSettings::SaveGlobalState(state); },
			[](const std::filesystem::path& root, const WorkspaceState& state) {
				WorkspaceSettings{root}.SaveState(state);
			}};
	};
}