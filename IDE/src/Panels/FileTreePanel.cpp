#include "Panels/FileTreePanel.h"

#include "Core/Base/Log.h"
#include "Core/Fs/FileSystem.h"

namespace ide
{
    FileTreePanel::FileTreePanel(Workspace& workspace)
        : core::Panel("files", "Files"), m_workspace(workspace)
    {
    }

    void FileTreePanel::OnUIRender()
    {
        RenderToolbar();
        ImGui::Separator();

        if (!m_workspace.IsOpen())
        {
            RenderEmptyState();
            return;
        }

        if (ImGui::BeginChild("##fileTreeBody", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                              ImGuiWindowFlags_HorizontalScrollbar))
        {
            RenderNode(m_workspace.Tree().Root());
        }

        ImGui::EndChild();
    }

    void FileTreePanel::RenderToolbar()
    {
        if (ImGui::Button("Open Folder"))
            OpenFolderInteractively(m_workspace);

        ImGui::SameLine();

        if (ImGui::Button("Refresh") && m_workspace.IsOpen())
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
        ImGui::TextDisabled("No folder open.");
        ImGui::TextDisabled("Use File \xe2\x86\x92 Open Folder to pick one.");
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
