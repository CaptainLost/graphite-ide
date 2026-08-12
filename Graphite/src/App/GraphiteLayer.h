#pragma once

#include "Editor/DocumentWindows.h"
#include "Editor/EditorSettings.h"
#include "Editor/ModalQuestion.h"
#include "App/TitleBar.h"
#include "Workspace/Workspace.h"

#include "Core/App/Layer.h"
#include "Core/Base/RingBufferSink.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"
#include "Core/Command/MenuBar.h"
#include "Core/Command/ShortcutRouter.h"
#include "Core/Event/EventBus.h"
#include "Core/Gui/DockSpaceHost.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Ui/PanelManager.h"

#include <filesystem>
#include <memory>
#include <vector>

namespace Graphite {

	class GraphiteLayer final : public Core::Layer
	{
	public:
		GraphiteLayer(std::shared_ptr<Core::RingBufferSink> sink, std::filesystem::path initialFolder);

		void OnAttach() override;
		void OnUpdate(Core::Timestep ts) override;
		void OnUIRender() override;

		bool OnCloseRequested() override;

	private:
		enum class ClosePurpose
		{
			ExitApplication,
			CloseWorkspace
		};

		void RequestCloseConfirmation(ClosePurpose purpose);

		void FinishClose(ClosePurpose purpose);

		void RenderCloseConfirm();

		void UpdateFocusReload(Core::Timestep ts);

		static constexpr float kReloadAfterSecondsAway = 2.0f;

		bool m_CustomTitleBar = false;

		bool m_AppForeground = true;
		float m_SecondsInBackground = 0.0f;

		bool m_CloseApproved = false;

		std::shared_ptr<Core::RingBufferSink> m_Sink;
		std::filesystem::path m_InitialFolder;
		Core::EventBus m_Events;
		Workspace m_Workspace{m_Events};
		Core::DocumentSet m_Documents;

		ModalGate m_Modals;
		DocumentWindows m_DocumentWindows{m_Documents, m_Modals};

		static constexpr const char* kUnsavedWorkTitle = "Unsaved changes##ExitUnsavedWork";

		ModalQuestion<ClosePurpose> m_CloseQuestion;

		Core::PanelManager m_Panels;
		Core::DockSpaceHost m_DockSpace;
		Core::DockLayout m_Layout;
		TitleBar m_TitleBar;

		Core::CommandRegistry m_Commands;
		Core::Keymap m_Keys;
		Core::ShortcutRouter m_Router;
		Core::MenuBar m_MenuBar;

		std::vector<Core::MenuItem> m_MenuTree;

		EditorSettings m_EditorSettings;
	};
}