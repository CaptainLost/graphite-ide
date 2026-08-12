#include "corepch.h"

#include "Core/Gui/DockSpaceHost.h"

#include "Core/Base/Log.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace Core
{
    namespace
    {
        constexpr const char* kHostWindowName = "###CoreDockSpaceHost";
        constexpr const char* kDockSpaceName = "CoreDockSpace";

        void DockPanels(ImGuiID nodeId, const std::vector<std::string>& panelIds)
        {
            for (const std::string& id : panelIds)
                ImGui::DockBuilderDockWindow(id.c_str(), nodeId);
        }
    }

    void DockSpaceHost::Begin(const DockLayout& layout)
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

        // A window's menu-bar height is fixed the instant Begin() runs — it reads
        // FramePadding right there (window->MenuBarHeight in imgui.cpp) — so an override
        // has to be active across exactly this call and no more. It is popped again
        // immediately below, before DockSpace(): left pushed any longer it would also be
        // read by DockNodeCalcTabBarLayout, which sizes every *docked node's* own tab bar
        // off the same style var, and stretching those to the caption's height would push
        // every panel's and every document's content down inside its own tab.
        const bool overrideMenuBarHeight = layout.menuBarHeight > 0.0f;
        if (overrideMenuBarHeight)
        {
            const ImGuiStyle& style = ImGui::GetStyle();
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                                ImVec2(style.FramePadding.x,
                                       (layout.menuBarHeight - ImGui::GetFontSize()) * 0.5f));
        }

        ImGui::Begin(kHostWindowName, nullptr, hostFlags);

        if (overrideMenuBarHeight)
        {
            ImGui::PopStyleVar();
        }

        ImGui::PopStyleVar(3);

        const ImGuiID dockSpaceId = ImGui::GetID(kDockSpaceName);
        m_dockSpaceId = dockSpaceId;
        ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

        if (m_layoutBuilt)
            return;

        m_layoutBuilt = true;

        // The layout saved in imgui.ini takes precedence at startup — we only build an
        // empty node. An explicit reset bypasses this guard, because that's exactly when we
        // want to overwrite what the user currently has on screen.
        const bool forced = m_forceRebuild;
        m_forceRebuild = false;

        if (!forced)
        {
            ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockSpaceId);
            if (node != nullptr && node->IsSplitNode())
                return;
        }

        ImGui::DockBuilderRemoveNode(dockSpaceId);
        ImGui::DockBuilderAddNode(dockSpaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockSpaceId, viewport->WorkSize);

        ImGuiID centerId = dockSpaceId;
        const ImGuiID leftId =
            ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Left, layout.leftRatio, nullptr, &centerId);
        const ImGuiID bottomId = ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Down,
                                                             layout.bottomRatio, nullptr, &centerId);

        DockPanels(leftId, layout.left);
        DockPanels(bottomId, layout.bottom);
        DockPanels(centerId, layout.center);

        // Flagged so CenterNodeId() can find it later without recomputing the split tree,
        // and so a caller with nothing else to go on - such as a document window opened
        // before anything has been focused - has somewhere to land. The flag is part of
        // what imgui.ini saves, so it survives into sessions that only restore the layout
        // rather than building it.
        if (ImGuiDockNode* centerNode = ImGui::DockBuilderGetNode(centerId); centerNode != nullptr)
            centerNode->SetLocalFlags(centerNode->LocalFlags | ImGuiDockNodeFlags_CentralNode);

        ImGui::DockBuilderFinish(dockSpaceId);
        CORE_INFO("Built default dock layout");
    }

    void DockSpaceHost::End()
    {
        ImGui::End();
    }

    unsigned int DockSpaceHost::CenterNodeId() const
    {
        if (m_dockSpaceId == 0)
            return 0;

        const ImGuiDockNode* centerNode = ImGui::DockBuilderGetCentralNode(m_dockSpaceId);
        return centerNode != nullptr ? centerNode->ID : 0;
    }
}
