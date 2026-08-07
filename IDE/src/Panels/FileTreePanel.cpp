#include "Panels/FileTreePanel.h"

#include "Core/Base/Log.h"
#include "Core/Fs/FileSystem.h"
#include "Core/Platform/Shell.h"

namespace ide
{
    FileTreePanel::FileTreePanel(Workspace& workspace)
        : core::Panel("files", "Files"), m_workspace(workspace)
    {
    }

    void FileTreePanel::OnUIRender()
    {
        if (!m_workspace.IsOpen())
        {
            RenderEmptyState();
            return;
        }

        RenderToolbar();
        ImGui::Separator();

        if (ImGui::BeginChild("##fileTreeBody", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                              ImGuiWindowFlags_HorizontalScrollbar))
        {
            RenderNode(m_workspace.Tree().Root());
        }

        ImGui::EndChild();
    }

    void FileTreePanel::RenderToolbar()
    {
        if (ImGui::Button("Refresh"))
        {
            core::FileNode& root = m_workspace.Tree().Root();
            m_workspace.Tree().Refresh(root);

            if (root.accessible)
                CORE_INFO("File tree refreshed");
            else
                CORE_WARN("File tree refresh failed: the workspace folder is unreadable");
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        m_filter.Draw("##fileFilter");
    }

    void FileTreePanel::RenderEmptyState()
    {
        constexpr const char* kMessage = "No folder open.";
        const ImVec2 buttonSize{140.0f, 0.0f};
        const float available = ImGui::GetContentRegionAvail().x;

        // Sit the block a third of the way down so it reads as an invitation rather than
        // a caption pinned under the tab bar.
        ImGui::Dummy(ImVec2(0.0f, ImGui::GetContentRegionAvail().y * 0.33f));

        const float messageWidth = ImGui::CalcTextSize(kMessage).x;
        if (available > messageWidth)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available - messageWidth) * 0.5f);

        ImGui::TextDisabled("%s", kMessage);
        ImGui::Spacing();

        if (available > buttonSize.x)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available - buttonSize.x) * 0.5f);

        if (ImGui::Button("Open Folder", buttonSize))
            OpenFolderInteractively(m_workspace);
    }

    bool FileTreePanel::PassesFilter(const core::FileNode& node) const
    {
        if (!m_filter.IsActive())
            return true;

        if (m_filter.PassFilter(node.name.c_str()))
            return true;

        // A directory stays visible when anything below it matches, otherwise the
        // filter would hide the path leading to the hit.
        for (const core::FileNode& child : node.children)
        {
            if (PassesFilter(child))
                return true;
        }

        return false;
    }

    void FileTreePanel::RenderContextMenu(const core::FileNode& node)
    {
        if (!ImGui::BeginPopupContextItem())
            return;

        if (ImGui::MenuItem("Show in Explorer"))
        {
            if (const auto revealed = core::Shell::RevealInFileBrowser(node.path); !revealed)
                CORE_ERROR("{}", revealed.GetError().message);
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Copy Path"))
            ImGui::SetClipboardText(core::fs::ToUtf8(node.path).c_str());

        if (ImGui::MenuItem("Copy Relative Path"))
        {
            std::error_code code;
            const std::filesystem::path relative =
                std::filesystem::relative(node.path, m_workspace.Root(), code);

            // No common prefix means relative() fails; the absolute path is still useful.
            ImGui::SetClipboardText(core::fs::ToUtf8(code ? node.path : relative).c_str());
        }

        ImGui::EndPopup();
    }

    // Must not mutate the tree while walking it: this recurses over node.children by
    // reference, and today the only mutating actions (opening a folder, refreshing)
    // happen in RenderToolbar, strictly before the walk starts. A future right-click
    // context menu that mutates the tree from inside RenderNode (e.g. delete, rename)
    // would invalidate the very vector being iterated.
    void FileTreePanel::RenderNode(core::FileNode& node)
    {
        if (!PassesFilter(node))
            return;

        if (!node.isDirectory)
        {
            const bool selected = m_workspace.SelectedFile() == node.path;

            if (ImGui::Selectable(node.name.c_str(), selected))
                m_workspace.SelectFile(node.path);

            RenderContextMenu(node);

            return;
        }

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (node.expanded)
            flags |= ImGuiTreeNodeFlags_DefaultOpen;

        if (!node.accessible)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

        const bool open =
            ImGui::TreeNodeEx(core::fs::ToUtf8(node.path).c_str(), flags, "%s", node.name.c_str());

        if (!node.accessible)
        {
            ImGui::PopStyleColor();

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("This folder could not be read.");
        }

        RenderContextMenu(node);

        if (open)
        {
            if (!node.expanded)
                (void)m_workspace.Tree().Expand(node);

            for (core::FileNode& child : node.children)
                RenderNode(child);

            ImGui::TreePop();
        }
        else if (node.expanded)
        {
            m_workspace.Tree().Collapse(node);
        }
    }
}
