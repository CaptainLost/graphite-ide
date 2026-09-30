#include "corepch.h"

#include "Core/ImGui/DockSpaceHost.h"

#include "Core/Base/Log.h"
#include "Core/ImGui/EmptyState.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace Core {

	namespace {
		constexpr const char* kHostWindowName = "###CoreDockSpaceHost";
		constexpr const char* kDockSpaceName = "CoreDockSpace";

		void DockPanels(ImGuiID nodeId, const std::vector<std::string>& panelIds)
		{
			for (const std::string& id : panelIds)
				ImGui::DockBuilderDockWindow(id.c_str(), nodeId);
		}

		bool WasRestoredFromIni(ImGuiID dockSpaceId)
		{
			const ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockSpaceId);

			return node != nullptr && node->IsSplitNode();
		}

		void BuildDefaultLayout(ImGuiID dockSpaceId, const DockLayout& layout, ImVec2 size)
		{
			ImGui::DockBuilderRemoveNode(dockSpaceId);
			ImGui::DockBuilderAddNode(dockSpaceId, ImGuiDockNodeFlags_DockSpace);
			ImGui::DockBuilderSetNodeSize(dockSpaceId, size);

			ImGuiID centerId = dockSpaceId;
			const ImGuiID leftId = ImGui::DockBuilderSplitNode(
				centerId, ImGuiDir_Left, layout.LeftRatio, nullptr, &centerId);
			const ImGuiID bottomId = ImGui::DockBuilderSplitNode(
				centerId, ImGuiDir_Down, layout.BottomRatio, nullptr, &centerId);

			DockPanels(leftId, layout.Left);
			DockPanels(bottomId, layout.Bottom);
			DockPanels(centerId, layout.Center);

			// Lets GetCenterNodeId find it later without recomputing the split tree, and gives
			// a caller with nothing else to go on somewhere to land. imgui.ini saves the flag,
			// so it survives into sessions that only restore the layout.
			if (ImGuiDockNode* centerNode = ImGui::DockBuilderGetNode(centerId); centerNode != nullptr)
				centerNode->SetLocalFlags(centerNode->LocalFlags | ImGuiDockNodeFlags_CentralNode);

			ImGui::DockBuilderFinish(dockSpaceId);
			CORE_INFO("Built default dock layout");
		}
	}

	void DockSpaceHost::Begin(const DockLayout& layout, const std::function<void()>& aboveDockSpace)
	{
		const ImGuiViewport* viewport = ImGui::GetMainViewport();

		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::SetNextWindowViewport(viewport->ID);

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

		constexpr ImGuiWindowFlags hostFlags =
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
			ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_MenuBar;

		// A window's menu-bar height is fixed the instant Begin() runs, from FramePadding, so
		// the override has to span exactly this call. Left pushed as far as DockSpace() it
		// would also size every docked node's own tab bar and push each panel's content down.
		const bool overrideMenuBarHeight = layout.MenuBarHeight > 0.0f;
		if (overrideMenuBarHeight)
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
								ImVec2(style.FramePadding.x,
									   (layout.MenuBarHeight - ImGui::GetFontSize()) * 0.5f));
		}

		ImGui::Begin(kHostWindowName, nullptr, hostFlags);

		if (overrideMenuBarHeight)
		{
			ImGui::PopStyleVar();
		}

		ImGui::PopStyleVar(3);

		if (aboveDockSpace)
		{
			aboveDockSpace();
		}

		const ImGuiID dockSpaceId = ImGui::GetID(kDockSpaceName);
		m_DockSpaceId = dockSpaceId;
		ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

		if (m_LayoutBuilt)
			return;

		m_LayoutBuilt = true;

		// A layout restored from imgui.ini takes precedence at startup, but an explicit reset
		// is exactly the request to overwrite what the user has on screen.
		const bool forced = m_ForceRebuild;
		m_ForceRebuild = false;

		if (!forced && WasRestoredFromIni(dockSpaceId))
			return;

		BuildDefaultLayout(dockSpaceId, layout, viewport->WorkSize);
	}

	void DockSpaceHost::End()
	{
		ImGui::End();
	}

	unsigned int DockSpaceHost::GetCenterNodeId() const
	{
		if (m_DockSpaceId == 0)
			return 0;

		const ImGuiDockNode* centerNode = ImGui::DockBuilderGetCentralNode(m_DockSpaceId);
		return centerNode != nullptr ? centerNode->ID : 0;
	}

	bool DockSpaceHost::IsCenterEmpty() const
	{
		const ImGuiDockNode* node = ImGui::DockBuilderGetNode(GetCenterNodeId());

		return node != nullptr && node->IsEmpty();
	}

	bool DockSpaceHost::CenterEmptyState(std::string_view message, const char* action) const
	{
		if (!IsCenterEmpty())
			return false;

		// Into the window DockSpace() opened inside the host, not the host itself: that window
		// paints the empty node's background over anything the host drew there. Beginning it
		// again this frame appends to it, above that background, and it is also the window
		// the node's clicks go to. Every docked window draws above it in turn, so the message
		// cannot land on a tab that arrives later in the frame.
		const ImGuiDockNode* node = ImGui::DockBuilderGetNode(GetCenterNodeId());
		ImGuiWindow* dockSpaceWindow = node->HostWindow;

		if (dockSpaceWindow == nullptr)
			return false;

		ImGui::Begin(dockSpaceWindow->Name, nullptr, dockSpaceWindow->Flags);

		ImGui::SetCursorScreenPos(node->Pos);
		const bool clicked = EmptyState(message, action, node->Size);

		ImGui::End();

		return clicked;
	}
}