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
			for (const std::string& panelId : panelIds)
			{
				ImGui::DockBuilderDockWindow(panelId.c_str(), nodeId);
			}
		}

		bool WasRestoredFromIni(ImGuiID dockSpaceId)
		{
			const ImGuiDockNode* dockSpaceNode = ImGui::DockBuilderGetNode(dockSpaceId);

			return dockSpaceNode != nullptr && dockSpaceNode->IsSplitNode();
		}

		void BuildDefaultLayout(ImGuiID dockSpaceId, const DockLayout& layout, ImVec2 hostSize)
		{
			ImGui::DockBuilderRemoveNode(dockSpaceId);
			ImGui::DockBuilderAddNode(dockSpaceId, ImGuiDockNodeFlags_DockSpace);
			ImGui::DockBuilderSetNodeSize(dockSpaceId, hostSize);

			ImGuiID centerId = dockSpaceId;
			const ImGuiID leftId = ImGui::DockBuilderSplitNode(
				centerId, ImGuiDir_Left, layout.LeftRatio, nullptr, &centerId);
			const ImGuiID bottomId = ImGui::DockBuilderSplitNode(
				centerId, ImGuiDir_Down, layout.BottomRatio, nullptr, &centerId);

			DockPanels(leftId, layout.Left);
			DockPanels(bottomId, layout.Bottom);
			DockPanels(centerId, layout.Center);

			if (ImGuiDockNode* centerNode = ImGui::DockBuilderGetNode(centerId); centerNode != nullptr)
			{
				centerNode->SetLocalFlags(centerNode->LocalFlags | ImGuiDockNodeFlags_CentralNode);
			}

			ImGui::DockBuilderFinish(dockSpaceId);
			CORE_INFO("Built default dock layout");
		}

		// WARNING: ImGui selects a docked tab on a right press or release before any owner can ask about the click, and the tab bars are submitted inside DockSpace. The right button is locked for exactly that call and restored straight after, so the tab's own context menu still sees the click.
		void DockSpaceIgnoringRightClick(ImGuiID dockSpaceId)
		{
			ImGuiKeyOwnerData* rightButtonOwner = ImGui::GetKeyOwnerData(ImGui::GetCurrentContext(), ImGuiKey_MouseRight);
			const ImGuiKeyOwnerData ownerBeforeLock = *rightButtonOwner;
			rightButtonOwner->LockThisFrame = true;

			const ImVec2 sizeFillingWindow(0.0f, 0.0f);
			ImGui::DockSpace(dockSpaceId, sizeFillingWindow, ImGuiDockNodeFlags_None);

			*rightButtonOwner = ownerBeforeLock;
		}
	}

	void DockSpaceHost::Begin(const DockLayout& layout, const std::function<void()>& aboveDockSpace,
							 const std::function<void()>& besideDockSpace)
	{
		m_BuiltLayoutThisFrame = false;

		const ImGuiViewport* viewport = ImGui::GetMainViewport();

		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::SetNextWindowViewport(viewport->ID);

		constexpr float kNoRounding = 0.0f;
		constexpr float kNoBorder = 0.0f;
		const ImVec2 noPadding(0.0f, 0.0f);
		constexpr int hostStyleVarCount = 3;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, kNoRounding);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, kNoBorder);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, noPadding);

		constexpr ImGuiWindowFlags hostFlags =
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
			ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_MenuBar;

		// WARNING: a window's menu-bar height is fixed when Begin runs, from FramePadding, so the override must span exactly that call. Pushed as far as DockSpace it would make every docked node's tab bar taller too, but each docked window computes its TitleBarHeight in its own Begin, so the bar would grow under the window and the content would not move down.
		const bool overrideMenuBarHeight = layout.MenuBarHeight > 0.0f;
		if (overrideMenuBarHeight)
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			const float paddingCentringText = (layout.MenuBarHeight - ImGui::GetFontSize()) * 0.5f;
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
								ImVec2(style.FramePadding.x, paddingCentringText));
		}

		ImGui::Begin(kHostWindowName, nullptr, hostFlags);

		if (overrideMenuBarHeight)
		{
			ImGui::PopStyleVar();
		}

		ImGui::PopStyleVar(hostStyleVarCount);

		if (aboveDockSpace)
		{
			aboveDockSpace();
		}

		if (layout.StripWidth > 0.0f && besideDockSpace)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, noPadding);
			ImGui::BeginChild("##CoreDockSpaceStrip", ImVec2(layout.StripWidth, 0.0f), ImGuiChildFlags_None,
							  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
			ImGui::PopStyleVar();
			besideDockSpace();
			ImGui::EndChild();
			ImGui::SameLine(0.0f, 0.0f);
		}

		const ImGuiID dockSpaceId = ImGui::GetID(kDockSpaceName);
		m_DockSpaceId = dockSpaceId;
		DockSpaceIgnoringRightClick(dockSpaceId);

		if (m_LayoutBuilt)
		{
			return;
		}

		m_LayoutBuilt = true;

		const bool layoutResetRequested = m_ForceRebuild;
		m_ForceRebuild = false;

		const bool keepRestoredLayout = !layoutResetRequested && WasRestoredFromIni(dockSpaceId);
		if (keepRestoredLayout)
		{
			return;
		}

		BuildDefaultLayout(dockSpaceId, layout, viewport->WorkSize);
		m_BuiltLayoutThisFrame = true;
	}

	void DockSpaceHost::End()
	{
		ImGui::End();
	}

	unsigned int DockSpaceHost::GetCenterNodeId() const
	{
		if (m_DockSpaceId == 0)
		{
			return 0;
		}

		const ImGuiDockNode* centerNode = ImGui::DockBuilderGetCentralNode(m_DockSpaceId);
		return centerNode != nullptr ? centerNode->ID : 0;
	}

	bool DockSpaceHost::IsCenterEmpty() const
	{
		const ImGuiDockNode* centerNode = ImGui::DockBuilderGetNode(GetCenterNodeId());

		return centerNode != nullptr && centerNode->IsEmpty();
	}

	bool DockSpaceHost::CenterEmptyState(std::string_view message, const char* action) const
	{
		if (!IsCenterEmpty())
		{
			return false;
		}

		// WARNING: this draws into the window DockSpace opened inside the host, not the host: that window paints the empty node's background over anything the host drew there. Beginning it again appends above that background, and every docked window draws above it, so the message cannot land on a later tab.
		const ImGuiDockNode* centerNode = ImGui::DockBuilderGetNode(GetCenterNodeId());
		ImGuiWindow* dockSpaceWindow = centerNode->HostWindow;

		if (dockSpaceWindow == nullptr)
		{
			return false;
		}

		ImGui::Begin(dockSpaceWindow->Name, nullptr, dockSpaceWindow->Flags);

		ImGui::SetCursorScreenPos(centerNode->Pos);
		const bool clicked = EmptyState(message, action, centerNode->Size);

		ImGui::End();

		return clicked;
	}
}