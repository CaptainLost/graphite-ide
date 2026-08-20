#pragma once

#include "Core/Base/Base.h"

#include "Editor/DocumentWindows.h"
#include "Editor/EditorSettings.h"
#include "Editor/ModalQuestion.h"
#include "App/TitleBar.h"
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
#include "Core/Text/DocumentSet.h"
#include "Core/UI/PanelManager.h"

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
		// Declared after m_LanguageServices, which it holds by reference: it opens each document
		// with the matching server and feeds the reference-count rows from the answers.
		DocumentWindows m_DocumentWindows{m_Documents, m_Modals, m_LanguageServices};

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

		static constexpr const char* kUnsavedWorkTitle = "Unsaved changes##ExitUnsavedWork";

		ModalQuestion<ClosePurpose> m_CloseQuestion;

		static constexpr const char* kReplaceWorkspaceTitle =
			"Open dropped folder##DropReplacesWorkspace";

		ModalQuestion<DroppedWorkspace> m_DropQuestion;

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