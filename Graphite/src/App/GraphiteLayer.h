#pragma once

#include "Core/Base/Base.h"

#include "Editor/DocumentWindows.h"
#include "Editor/EditorSettings.h"
#include "Editor/LanguageServicesPort.h"
#include "Editor/ModalQuestion.h"
#include "Editor/PreviewSemantics.h"
#include "App/TitleBar.h"
#include "Git/DiffWindows.h"
#include "Git/GitModel.h"
#include "Git/RepositoryGitPort.h"
#include "Lang/Server/LanguageServices.h"
#include "Search/SearchService.h"
#include "Search/SearchSettings.h"
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

		void QueueForFocusedEditor(Core::Event& event);

		void RequestCloseConfirmation(ClosePurpose purpose);

		void FinishClose(ClosePurpose purpose);

		void RenderCloseConfirm();

		bool ConfirmationStillNeeded(ClosePurpose purpose);

		void ListModifiedDocuments() const;

		ClosePurpose AskedPurposeOrExit() const;

		void UpdateFocusReload(Core::Timestep ts);

		void ApplyChromeIfSchemeChanged();

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

		ModalGate m_Modals;

		// Declared after m_LanguageServices, which the port forwards to, and before
		// m_DocumentWindows and m_Panels, both of which hold this by reference.
		LanguageServicesPort m_PreviewPort{m_LanguageServices};
		PreviewSemantics m_PreviewSemantics{m_PreviewPort};

		// Declared after m_LanguageServices, which it holds by reference: it opens each document
		// with the matching server and feeds the reference-count rows from the answers.
		DocumentWindows m_DocumentWindows{m_Documents, m_Modals, m_LanguageServices,
										  m_PreviewSemantics};

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
		// The order among these three is not the declaration order the naive reading suggests.
		// Every job GitModel submits captures `this`, so the queue behind the submitter must
		// be destroyed - and its workers joined - before the model goes away, and the port
		// must outlive every query a worker might still be running against it. Members are
		// destroyed in reverse declaration order, so m_Jobs is declared LAST of the three: its
		// destructor runs FIRST, joining every worker before m_Git or m_GitPort can be torn
		// down. Declaring m_Jobs first would compile without complaint and fail only under
		// real threading, when a worker is mid-query as the model it points into disappears.
		//
		// m_Events, declared far above, is a related trap: it is destroyed AFTER m_Git even
		// though m_Git subscribes handlers into it that capture `this`. That is not reachable
		// today - nothing publishes from a destructor and EventBus has no unsubscribe - but a
		// member declared between m_Events and this group must not change that, so it is
		// recorded here rather than left for the next reader to rediscover.
		RepositoryGitPort m_GitPort{std::filesystem::path{}};
		GitModel m_Git{m_GitPort,
					   [this](std::function<void()> job) { m_Jobs.Submit(std::move(job)); }};
		Core::JobQueue m_Jobs;

		// Holds no reference to m_Git or the queue above, so its place in this group is only
		// about outliving what refers to it: GitView takes a DiffWindows&, so this must be
		// declared before m_Panels, which is where GitView is constructed.
		DiffWindows m_DiffWindows;

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
	};
}