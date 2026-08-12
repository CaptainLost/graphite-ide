#include "Panels/FileTreeView.h"

#include "Core/Base/Log.h"
#include "Core/Fs/FileOperations.h"
#include "Core/Fs/FileSystem.h"
#include "Core/Gui/Icons.h"
#include "Core/Platform/Shell.h"
#include "Core/Text/DocumentSet.h"

#include <IconsMaterialDesignIcons.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace Graphite
{
    FileTreeView::FileTreeView(Workspace& workspace, const Core::DocumentSet& documents)
        : m_workspace(workspace), m_documents(documents)
    {
    }

    void FileTreeView::Render()
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

    void FileTreeView::RenderToolbar()
    {
        if (ImGui::Button(ICON_MDI_REFRESH))
        {
            m_workspace.Reload();
            Core::FileNode& root = m_workspace.Tree().Root();

            if (root.accessible)
                CORE_INFO("File tree refreshed");
            else
                CORE_WARN("File tree refresh failed: the workspace folder is unreadable");
        }

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Refresh");

        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);

        if (ImGui::InputTextWithHint("##fileFilter", ICON_MDI_MAGNIFY "  Filter files",
                                     m_filter.InputBuf, IM_ARRAYSIZE(m_filter.InputBuf)))
        {
            m_filter.Build();
        }
    }

    void FileTreeView::RenderEmptyState()
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

    bool FileTreeView::PassesFilter(const Core::FileNode& node) const
    {
        if (!m_filter.IsActive())
            return true;

        if (m_filter.PassFilter(node.name.c_str()))
            return true;

        // A directory stays visible when anything below it matches, otherwise the
        // filter would hide the path leading to the hit.
        for (const Core::FileNode& child : node.children)
        {
            if (PassesFilter(child))
                return true;
        }

        return false;
    }

    void FileTreeView::RenderContextMenu(const Core::FileNode& node)
    {
        // BeginPopupContextItem cannot pass window flags, and without NoMove ImGui lets
        // the menu be dragged anywhere by its body — a popup has no title bar, so its
        // whole surface counts as draggable background. ConfigWindowsMoveFromTitleBarOnly
        // does not help: its cancel path is skipped for windows that have no title bar.
        // So open the popup by hand and use BeginPopup, which does take window flags.
        const std::string popupId = "##ctx" + Core::ToUtf8(node.path);

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
        {
            ImGui::OpenPopup(popupId.c_str());
        }

        if (!ImGui::BeginPopup(popupId.c_str(), ImGuiWindowFlags_NoMove))
            return;

        // The workspace root has no parent inside the tree, so renaming, deleting or
        // duplicating it would leave the panel pointing at a path that no longer exists.
        const bool isRoot = m_workspace.IsOpen() && node.path == m_workspace.Root();

        if (ImGui::MenuItem("Show in Explorer"))
        {
            if (const auto revealed = Core::Shell::RevealInFileBrowser(node.path); !revealed)
                CORE_ERROR("{}", revealed.GetError().message);
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Copy Path"))
            ImGui::SetClipboardText(Core::ToUtf8(node.path).c_str());

        if (ImGui::MenuItem("Copy Relative Path"))
        {
            std::error_code code;
            const std::filesystem::path relative =
                std::filesystem::relative(node.path, m_workspace.Root(), code);

            // No common prefix means relative() fails; the absolute path is still useful.
            ImGui::SetClipboardText(Core::ToUtf8(code ? node.path : relative).c_str());
        }

        if (!isRoot)
        {
            ImGui::Separator();

            if (ImGui::MenuItem("Duplicate"))
                RecordAction(PendingAction{PendingAction::Kind::Duplicate, node.path, {}});
        }

        // Creating from a file means creating beside it, in its folder — the same thing
        // Explorer and VS Code do. Restricting this to directories would force the user
        // to hunt for the parent row just to make a sibling.
        const std::filesystem::path creationTarget =
            node.isDirectory ? node.path : node.path.parent_path();

        ImGui::Separator();

        if (ImGui::MenuItem("New File"))
            RecordAction(
                PendingAction{PendingAction::Kind::CreateFile, creationTarget, "new file.txt"});

        if (ImGui::MenuItem("New Folder"))
            RecordAction(
                PendingAction{PendingAction::Kind::MakeDirectory, creationTarget, "new folder"});

        if (!isRoot)
        {
            ImGui::Separator();

            if (ImGui::MenuItem("Rename"))
            {
                const std::string current = Core::ToUtf8(node.path.filename());

                if (current.size() >= m_renameBuffer.size())
                {
                    CORE_ERROR("Cannot rename '{}': the name is too long to edit", current);
                }
                else
                {
                    m_renamingPath = node.path;
                    m_focusRenameField = true;

                    std::fill(m_renameBuffer.begin(), m_renameBuffer.end(), '\0');
                    std::copy_n(current.begin(), current.size(), m_renameBuffer.begin());
                }
            }

            if (ImGui::MenuItem("Delete"))
                RecordAction(PendingAction{PendingAction::Kind::Delete, node.path, {}});
        }

        if (node.isDirectory)
        {
            ImGui::Separator();

            if (ImGui::MenuItem("Refresh"))
                RecordAction(PendingAction{PendingAction::Kind::Refresh, node.path, {}});
        }

        ImGui::EndPopup();
    }

    Core::FileNode* FileTreeView::FindNode(const std::filesystem::path& path)
    {
        if (!m_workspace.IsOpen())
            return nullptr;

        // Depth-first over an explicit stack: the tree is only as deep as the user has
        // expanded it, and this avoids a recursive helper for a two-line job.
        std::vector<Core::FileNode*> stack{&m_workspace.Tree().Root()};

        while (!stack.empty())
        {
            Core::FileNode* node = stack.back();
            stack.pop_back();

            if (node->path == path)
                return node;

            for (Core::FileNode& child : node->children)
                stack.push_back(&child);
        }

        return nullptr;
    }

    void FileTreeView::RefreshParentOf(const std::filesystem::path& path)
    {
        Core::FileNode* parent = FindNode(path.parent_path());

        // Outside the tree, or the parent was never loaded: nothing on screen to update.
        if (parent != nullptr)
            m_workspace.Tree().Refresh(*parent);
    }

    void FileTreeView::RecordAction(PendingAction action)
    {
        // One slot, two producers: the context menu and the rename editor can both fire
        // in the same frame. Assigning the whole struct keeps stale fields from leaking
        // between kinds, and the warning makes a dropped request visible instead of
        // silent.
        if (m_pending.kind != PendingAction::Kind::None)
        {
            CORE_WARN("Discarding a pending file action that was replaced before it ran");
        }

        m_pending = std::move(action);
    }

    void FileTreeView::ExecutePendingAction()
    {
        const PendingAction action = m_pending;
        m_pending = PendingAction{};

        switch (action.kind)
        {
        case PendingAction::Kind::None:      return;
        case PendingAction::Kind::Delete:    HandleDelete(action); return;
        case PendingAction::Kind::Refresh:   HandleRefresh(action); return;
        case PendingAction::Kind::Rename:    HandleRename(action); return;
        case PendingAction::Kind::Duplicate: HandleDuplicate(action); return;
        case PendingAction::Kind::CreateFile:
        case PendingAction::Kind::MakeDirectory:
            HandleCreate(action);
            return;
        default:
            return;
        }
    }

    void FileTreeView::HandleDelete(const PendingAction& action)
    {
        // Deletion is the one irreversible-looking action, so it goes through a
        // confirmation rather than straight to the filesystem.
        m_renamingPath.clear();
        m_deleteCandidate = action.target;
        m_openDeleteConfirmation = true;
    }

    void FileTreeView::HandleRefresh(const PendingAction& action)
    {
        if (Core::FileNode* node = FindNode(action.target); node != nullptr)
            m_workspace.Tree().Refresh(*node);
    }

    void FileTreeView::HandleRename(const PendingAction& action)
    {
        if (!Core::IsValidFileName(action.name))
        {
            CORE_ERROR("'{}' is not a valid file name", action.name);
            return;
        }

        const std::filesystem::path target =
            action.target.parent_path() / Core::FromUtf8(action.name);

        if (target == action.target)
            return;

        if (const auto renamed = Core::Rename(action.target, target); !renamed)
        {
            CORE_ERROR("{}", renamed.GetError().message);
            return;
        }

        RefreshParentOf(action.target);
    }

    void FileTreeView::HandleDuplicate(const PendingAction& action)
    {
        const auto copied = Core::Duplicate(action.target);

        if (!copied)
        {
            CORE_ERROR("{}", copied.GetError().message);
            return;
        }

        RefreshParentOf(action.target);
    }

    void FileTreeView::HandleCreate(const PendingAction& action)
    {
        if (!Core::IsValidFileName(action.name))
        {
            CORE_ERROR("'{}' is not a valid file name", action.name);
            return;
        }

        // MakeUniquePath keeps repeated clicks from failing on the default name.
        const std::filesystem::path target =
            Core::MakeUniquePath(action.target / Core::FromUtf8(action.name));

        const auto created = action.kind == PendingAction::Kind::CreateFile
                                 ? Core::CreateEmptyFile(target)
                                 : Core::MakeDirectory(target);

        if (!created)
        {
            CORE_ERROR("{}", created.GetError().message);
            return;
        }

        if (Core::FileNode* parent = FindNode(action.target); parent != nullptr)
        {
            // Load the folder if it was never opened, otherwise re-read it. Doing both
            // would read the same directory from disk twice.
            if (!parent->loaded)
                (void)m_workspace.Tree().Expand(*parent);
            else
                m_workspace.Tree().Refresh(*parent);

            parent->expanded = true;
            m_forceOpenPath = action.target;
        }

        CORE_INFO("Created '{}'", Core::ToUtf8(target));
    }

    void FileTreeView::RenderDeleteConfirmation()
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
                    Core::ToUtf8(m_deleteCandidate.filename()).c_str());
        ImGui::Spacing();

        if (ImGui::Button("Move"))
        {
            if (const auto removed = Core::MoveToRecycleBin(m_deleteCandidate); !removed)
                CORE_ERROR("{}", removed.GetError().message);
            else
                RefreshParentOf(m_deleteCandidate);

            m_deleteCandidate.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel"))
        {
            m_deleteCandidate.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    bool FileTreeView::RenderRenameEditor(const Core::FileNode& node)
    {
        if (m_renamingPath != node.path)
            return false;

        if (m_focusRenameField)
        {
            ImGui::SetKeyboardFocusHere();
            m_focusRenameField = false;
        }

        // A directory's label sits to the right of its arrow, so shift the field by the
        // same amount — otherwise the name visibly jumps left the moment editing starts.
        // Files have no arrow and need no shift.
        constexpr float kFieldPaddingX = 2.0f;
        const float indent =
            node.isDirectory ? ImGui::GetTreeNodeToLabelSpacing() - kFieldPaddingX : 0.0f;

        if (indent > 0.0f)
            ImGui::Indent(indent);

        // The theme's frame padding is sized for buttons and would make the edited row
        // taller than its neighbours, shoving the rest of the tree down.
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(kFieldPaddingX, 0.0f));

        // Fit the field to the name rather than the panel: a full-width box for a short
        // name reads as a dialog dropped into the tree. One character of slack leaves
        // room for the caret, and it grows as the user types.
        constexpr float kMinFieldWidth = 90.0f;
        const float textWidth = ImGui::CalcTextSize(m_renameBuffer.data()).x;
        const float fitted = textWidth + kFieldPaddingX * 2.0f + ImGui::GetFontSize();
        const float available = ImGui::GetContentRegionAvail().x;

        ImGui::SetNextItemWidth(std::clamp(fitted, kMinFieldWidth, available));

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

        ImGui::PopStyleVar();

        if (indent > 0.0f)
            ImGui::Unindent(indent);

        if (cancelled)
        {
            m_renamingPath.clear();
        }
        else if (committed || deactivated)
        {
            RecordAction(PendingAction{PendingAction::Kind::Rename, node.path, m_renameBuffer.data()});
            m_renamingPath.clear();
        }

        return true;
    }

    // RenderNode must not mutate the tree while walking it: it recurses over
    // node.children by reference. Context menu commands therefore record a
    // PendingAction instead of acting immediately; ExecutePendingAction runs the
    // recorded action after the walk finishes, once it is safe to mutate.
    void FileTreeView::RenderNode(Core::FileNode& node)
    {
        if (!PassesFilter(node))
            return;

        if (node.ignored && m_gitIgnoredDisplay == GitIgnoredDisplay::Hide)
        {
            return;
        }

        if (!node.isDirectory)
        {
            if (RenderRenameEditor(node))
                return;

            const bool dimmed = node.ignored && m_gitIgnoredDisplay == GitIgnoredDisplay::Dim;

            if (dimmed)
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

            // Which document is on screen, and which are merely open, are both facts the
            // DocumentSet owns - so they are read from it rather than from
            // Workspace::SelectedFile, which only ever moves when a row here is clicked and
            // so stayed put whenever the user switched tabs in the editor instead.
            const auto active = m_documents.Active();
            const bool isActive = active.has_value() && m_documents.At(*active).path == node.path;
            const bool isOpen = m_documents.Find(node.path).has_value();

            // An open document that is not the active one gets the same highlight at a
            // fraction of its opacity: enough to pick the open files out of a long folder,
            // never enough to compete with the one row saying "this is what you are
            // looking at". Drawn through Selectable's own selected state rather than a
            // hand-drawn rect so it lines up with the real highlight exactly.
            const bool faded = isOpen && !isActive;

            if (faded)
            {
                ImVec4 colour = ImGui::GetStyleColorVec4(ImGuiCol_Header);
                colour.w *= 0.45f;
                ImGui::PushStyleColor(ImGuiCol_Header, colour);
            }

            const std::string label =
                std::string{Core::IconForExtension(Core::ToUtf8(node.path.extension()))} +
                "  " + node.name;

            const bool clicked = ImGui::Selectable(label.c_str(), isActive || faded,
                                                   ImGuiSelectableFlags_AllowDoubleClick);

            if (faded)
                ImGui::PopStyleColor();

            if (clicked)
            {
                m_workspace.SelectFile(node.path);
                m_workspace.RequestOpenFile(node.path, ImGui::IsMouseDoubleClicked(0));
            }

            if (dimmed)
                ImGui::PopStyleColor();

            RenderContextMenu(node);

            return;
        }

        if (RenderRenameEditor(node))
            return;

        if (!m_forceOpenPath.empty() && m_forceOpenPath == node.path)
        {
            ImGui::SetNextItemOpen(true);
            m_forceOpenPath.clear();
        }

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
        if (node.expanded)
            flags |= ImGuiTreeNodeFlags_DefaultOpen;

        const bool dimmed =
            !node.accessible || (node.ignored && m_gitIgnoredDisplay == GitIgnoredDisplay::Dim);

        if (dimmed)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

        const bool open =
            ImGui::TreeNodeEx(Core::ToUtf8(node.path).c_str(), flags, "%s  %s",
                               node.expanded ? ICON_MDI_FOLDER_OPEN : ICON_MDI_FOLDER,
                               node.name.c_str());

        if (dimmed)
        {
            ImGui::PopStyleColor();

            if (!node.accessible && ImGui::IsItemHovered())
                ImGui::SetTooltip("This folder could not be read.");
        }

        RenderContextMenu(node);

        if (open)
        {
            if (!node.expanded)
                (void)m_workspace.Tree().Expand(node);

            for (Core::FileNode& child : node.children)
                RenderNode(child);

            ImGui::TreePop();
        }
        else if (node.expanded)
        {
            m_workspace.Tree().Collapse(node);
        }
    }
}
