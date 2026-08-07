#include "corepch.h"

#include "Core/Gui/DockSpaceHost.h"

#include "Core/Base/Log.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace core
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

        ImGui::Begin(kHostWindowName, nullptr, hostFlags);
        ImGui::PopStyleVar(3);

        const ImGuiID dockSpaceId = ImGui::GetID(kDockSpaceName);
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

        ImGui::DockBuilderFinish(dockSpaceId);
        CORE_INFO("Built default dock layout");
    }

    void DockSpaceHost::End()
    {
        ImGui::End();
    }
}
