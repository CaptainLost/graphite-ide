#include "Panels/FileTreePanel.h"

#include "Core/Base/Log.h"
#include "Core/Fs/FileOperations.h"
#include "Core/Fs/FileSystem.h"
#include "Core/Platform/Shell.h"

#include <algorithm>
#include <vector>

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

        // Both run outside the walk: the executor mutates the tree, and the modal must
        // not be nested inside the child window.
        ExecutePendingAction();
        RenderDeleteConfirmation();
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

        ImGui::Separator();

        if (ImGui::MenuItem("Duplicate"))
        {
            m_pending.kind = PendingAction::Kind::Duplicate;
            m_pending.target = node.path;
        }

        if (node.isDirectory)
        {
            ImGui::Separator();

            if (ImGui::MenuItem("New File"))
            {
                m_pending.kind = PendingAction::Kind::CreateFile;
                m_pending.target = node.path;
                m_pending.name = "new file.txt";
            }

            if (ImGui::MenuItem("New Folder"))
            {
                m_pending.kind = PendingAction::Kind::MakeDirectory;
                m_pending.target = node.path;
                m_pending.name = "new folder";
            }
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Rename"))
        {
            m_renamingPath = node.path;
            m_focusRenameField = true;

            const std::string current = core::fs::ToUtf8(node.path.filename());
            const std::size_t copied = std::min(current.size(), m_renameBuffer.size() - 1);

            std::fill(m_renameBuffer.begin(), m_renameBuffer.end(), '\0');
            std::copy_n(current.begin(), copied, m_renameBuffer.begin());
        }

        if (ImGui::MenuItem("Delete"))
        {
            m_pending.kind = PendingAction::Kind::Delete;
            m_pending.target = node.path;
        }

        if (node.isDirectory)
        {
            ImGui::Separator();

            if (ImGui::MenuItem("Refresh"))
            {
                m_pending.kind = PendingAction::Kind::Refresh;
                m_pending.target = node.path;
            }
        }

        ImGui::EndPopup();
    }

    core::FileNode* FileTreePanel::FindNode(const std::filesystem::path& path)
    {
        if (!m_workspace.IsOpen())
            return nullptr;

        // Depth-first over an explicit stack: the tree is only as deep as the user has
        // expanded it, and this avoids a recursive helper for a two-line job.
        std::vector<core::FileNode*> stack{&m_workspace.Tree().Root()};

        while (!stack.empty())
        {
            core::FileNode* node = stack.back();
            stack.pop_back();

            if (node->path == path)
                return node;

            for (core::FileNode& child : node->children)
                stack.push_back(&child);
        }

        return nullptr;
    }

    void FileTreePanel::RefreshParentOf(const std::filesystem::path& path)
    {
        core::FileNode* parent = FindNode(path.parent_path());

        // Outside the tree, or the parent was never loaded: nothing on screen to update.
        if (parent != nullptr)
            m_workspace.Tree().Refresh(*parent);
    }

    void FileTreePanel::ExecutePendingAction()
    {
        const PendingAction action = m_pending;
        m_pending = PendingAction{};

        switch (action.kind)
        {
        case PendingAction::Kind::None:
            return;

        case PendingAction::Kind::Delete:
            // Deletion is the one irreversible-looking action, so it goes through a
            // confirmation rather than straight to the filesystem.
            m_renamingPath.clear();
            m_deleteCandidate = action.target;
            m_openDeleteConfirmation = true;
            return;

        case PendingAction::Kind::Refresh:
        {
            if (core::FileNode* node = FindNode(action.target); node != nullptr)
                m_workspace.Tree().Refresh(*node);

            return;
        }

        case PendingAction::Kind::Rename:
        {
            if (!core::fs::IsValidFileName(action.name))
            {
                CORE_ERROR("'{}' is not a valid file name", action.name);
                return;
            }

            const std::filesystem::path target =
                action.target.parent_path() / core::fs::FromUtf8(action.name);

            if (target == action.target)
                return;

            if (const auto renamed = core::fs::Rename(action.target, target); !renamed)
            {
                CORE_ERROR("{}", renamed.GetError().message);
                return;
            }

            RefreshParentOf(action.target);
            return;
        }

        case PendingAction::Kind::Duplicate:
        {
            const auto copied = core::fs::Duplicate(action.target);

            if (!copied)
            {
                CORE_ERROR("{}", copied.GetError().message);
                return;
            }

            RefreshParentOf(action.target);
            return;
        }

        case PendingAction::Kind::CreateFile:
        case PendingAction::Kind::MakeDirectory:
        {
            if (!core::fs::IsValidFileName(action.name))
            {
                CORE_ERROR("'{}' is not a valid file name", action.name);
                return;
            }

            // MakeUniquePath keeps repeated clicks from failing on the default name.
            const std::filesystem::path target =
                core::fs::MakeUniquePath(action.target / core::fs::FromUtf8(action.name));

            const auto created = action.kind == PendingAction::Kind::CreateFile
                                     ? core::fs::CreateEmptyFile(target)
                                     : core::fs::MakeDirectory(target);

            if (!created)
            {
                CORE_ERROR("{}", created.GetError().message);
                return;
            }

            if (core::FileNode* parent = FindNode(action.target); parent != nullptr)
            {
                // Expand first so a folder the user never opened still shows what was
                // just created inside it.
                (void)m_workspace.Tree().Expand(*parent);
                m_workspace.Tree().Refresh(*parent);
            }

            return;
        }

        default:
            return;
        }
    }

    void FileTreePanel::RenderDeleteConfirmation()
    {
        if (m_openDeleteConfirmation)
        {
            ImGui::OpenPopup("Delete##fileTreeDelete");
            m_openDeleteConfirmation = false;
        }

        if (!ImGui::BeginPopupModal("Delete##fileTreeDelete", nullptr,
                                    ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }

        ImGui::Text("Move \"%s\" to the Recycle Bin?",
                    core::fs::ToUtf8(m_deleteCandidate.filename()).c_str());
        ImGui::Spacing();

        if (ImGui::Button("Move to Recycle Bin", ImVec2(160.0f, 0.0f)))
        {
            if (const auto removed = core::fs::MoveToRecycleBin(m_deleteCandidate); !removed)
                CORE_ERROR("{}", removed.GetError().message);
            else
                RefreshParentOf(m_deleteCandidate);

            m_deleteCandidate.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f)))
        {
            m_deleteCandidate.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    bool FileTreePanel::RenderRenameEditor(const core::FileNode& node)
    {
        if (m_renamingPath != node.path)
            return false;

        if (m_focusRenameField)
        {
            ImGui::SetKeyboardFocusHere();
            m_focusRenameField = false;
        }

        ImGui::SetNextItemWidth(-1.0f);

        const bool committed =
            ImGui::InputText("##renameField", m_renameBuffer.data(), m_renameBuffer.size(),
                             ImGuiInputTextFlags_EnterReturnsTrue |
                                 ImGuiInputTextFlags_AutoSelectAll);

        // Escape cancels, but only while this field owns the keyboard — an unscoped key
        // poll would also catch an Escape aimed at a modal elsewhere and silently throw
        // away what the user typed here.
        const bool cancelled = ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape);

        // Any other way of leaving the field commits, the way Explorer and VS Code behave.
        // IsItemDeactivated fires whether or not the text changed, so a rename opened by
        // mistake and clicked away still ends instead of leaving the row stuck.
        const bool deactivated = !cancelled && ImGui::IsItemDeactivated();

        if (cancelled)
        {
            m_renamingPath.clear();
            return true;
        }

        if (committed || deactivated)
        {
            m_pending.kind = PendingAction::Kind::Rename;
            m_pending.target = node.path;
            m_pending.name = m_renameBuffer.data();
            m_renamingPath.clear();
        }

        return true;
    }

    // RenderNode must not mutate the tree while walking it: it recurses over
    // node.children by reference. Context menu commands therefore record a
    // PendingAction instead of acting immediately; ExecutePendingAction runs the
    // recorded action after the walk finishes, once it is safe to mutate.
    void FileTreePanel::RenderNode(core::FileNode& node)
    {
        if (!PassesFilter(node))
            return;

        if (!node.isDirectory)
        {
            if (RenderRenameEditor(node))
                return;

            const bool selected = m_workspace.SelectedFile() == node.path;

            if (ImGui::Selectable(node.name.c_str(), selected))
                m_workspace.SelectFile(node.path);

            RenderContextMenu(node);

            return;
        }

        if (RenderRenameEditor(node))
            return;

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
