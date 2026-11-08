#include "corepch.h"

#include "Core/ImGui/DockSpaceHost.h"

#include "Core/Base/Log.h"
#include "Core/ImGui/DockTab.h"
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

		bool IsNamedByLayout(ImGuiID windowId, const DockLayout& layout)
		{
			for (const std::vector<std::string>* names : {&layout.Left, &layout.Bottom, &layout.Center})
			{
				for (const std::string& name : *names)
				{
					// Hashed as DockBuilderDockWindow hashes the name it is given.
					if (ImHashStr(name.c_str()) == windowId)
					{
						return true;
					}
				}
			}

			return false;
		}

		// Every window docked anywhere in the dock space that the layout does not place by name: a document, a diff, a
		// terminal. Removing the dock space's nodes would leave each floating where its node was.
		std::vector<std::string> WindowsTheLayoutDoesNotPlace(ImGuiID dockSpaceId, const DockLayout& layout)
		{
			std::vector<std::string> unplaced;

			for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
			{
				// WARNING: Not filtered on ImGuiWindowFlags_ChildWindow: ImGui sets it on every docked window.
				if (window->DockNode == nullptr)
				{
					continue;
				}

				const ImGuiDockNode* root = ImGui::DockNodeGetRootNode(window->DockNode);

				if (root == nullptr || root->ID != dockSpaceId || IsNamedByLayout(window->ID, layout))
				{
					continue;
				}

				unplaced.emplace_back(window->Name);
			}

			return unplaced;
		}

		void BuildDefaultLayout(ImGuiID dockSpaceId, const DockLayout& layout, ImVec2 hostSize)
		{
			const std::vector<std::string> unplaced = WindowsTheLayoutDoesNotPlace(dockSpaceId, layout);

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

			// WARNING: The rest go to the centre, where a document opens; a window that has a place of its own, such as a
			// terminal, moves there itself once the layout is rebuilt.
			DockPanels(centerId, unplaced);

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

		// WARNING: a window's menu-bar height is fixed when Begin runs, from FramePadding, so the override must span exactly that call. Pushed as far as DockSpace it would make every docked node's tab bar taller too, but each docked window computes its TitleBarHeight in its own Begin, so the bar would grow under the window and the content would not move down. The tab bars take their own padding, in DockSpace and in every docked window's Begin alike, through DockTabBarStyle and BeginDockedWindow.
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

		{
			const DockTabBarStyle flatTabs;
			DockSpaceIgnoringRightClick(dockSpaceId);
		}

		DrawDockTabBarLines(dockSpaceId);
		KeepFloatingTabBarsFlat();

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

	bool DockSpaceHost::CenterEmptyState(std::string_view message, const char* action,
										 const char* noteWhileDisabled) const
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
		const bool clicked = EmptyState(message, action, centerNode->Size, noteWhileDisabled);

		ImGui::End();

		return clicked;
	}
}