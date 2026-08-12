#pragma once

#include "Workspace/GitIgnoredDisplay.h"
#include "Workspace/Workspace.h"

#include "Core/Fs/FileTree.h"

#include <imgui.h>

#include <array>
#include <filesystem>
#include <string>

namespace Core
{
    class DocumentSet;
}

namespace Graphite
{
    /// Browses the open workspace folder. Holds no state of its own beyond the filter —
    /// the tree lives in the Workspace.
    ///
    /// Not a Panel: ExplorerPanel owns this and one other view, and switches between them.
    class FileTreeView final
    {
    public:
        /// `documents` is read, never touched: the tree marks which files are open and
        /// which one is on screen, and asks the set rather than keeping its own idea of
        /// either.
        FileTreeView(Workspace& workspace, const Core::DocumentSet& documents);

        void Render();

        void SetGitIgnoredDisplay(GitIgnoredDisplay display) noexcept { m_GitIgnoredDisplay = display; }

    private:
        /// A menu command recorded during the tree walk and executed afterwards. Acting
        /// immediately would mutate the node.children vector that RenderNode is
        /// iterating by reference.
        struct PendingAction
        {
            enum class Kind
            {
                None,
                Delete,
                Rename,
                CreateFile,
                MakeDirectory,
                Duplicate,
                Refresh
            };

            Kind kind = Kind::None;
            std::filesystem::path target;
            std::string name;   // the new name for Rename, CreateFile and MakeDirectory
        };

        void RenderToolbar();
        void RenderEmptyState();
        void RenderNode(Core::FileNode& node);
        void RenderContextMenu(const Core::FileNode& node);
        void RenderDeleteConfirmation();
        [[nodiscard]] bool RenderRenameEditor(const Core::FileNode& node);
        void RecordAction(PendingAction action);
        void ExecutePendingAction();
        void HandleDelete(const PendingAction& action);
        void HandleRefresh(const PendingAction& action);
        void HandleRename(const PendingAction& action);
        void HandleDuplicate(const PendingAction& action);
        void HandleCreate(const PendingAction& action);
        [[nodiscard]] Core::FileNode* FindNode(const std::filesystem::path& path);
        void RefreshParentOf(const std::filesystem::path& path);
        [[nodiscard]] bool PassesFilter(const Core::FileNode& node) const;

        Workspace& m_Workspace;
        const Core::DocumentSet& m_Documents;
        ImGuiTextFilter m_Filter;
        PendingAction m_Pending;
        std::filesystem::path m_DeleteCandidate;
        bool m_OpenDeleteConfirmation = false;

        /// Set by an operation that creates something inside a folder, so the next frame
        /// can force that folder open. node.expanded alone cannot do it: it reaches ImGui
        /// as DefaultOpen, which ImGui ignores once the node has storage.
        std::filesystem::path m_ForceOpenPath;

        /// Keyed by path rather than by a pointer to the node: a refresh can drop the
        /// node being edited, and a stale pointer would be written through.
        std::filesystem::path m_RenamingPath;

        /// 1024 bytes, not MAX_PATH: a 255-character name can reach roughly 1020 bytes
        /// in UTF-8, and a truncated name would be committed to disk by the blur path.
        std::array<char, 1024> m_RenameBuffer{};
        bool m_FocusRenameField = false;
        GitIgnoredDisplay m_GitIgnoredDisplay = GitIgnoredDisplay::Dim;
    };
}
