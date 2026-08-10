#pragma once

#include "IgnoredDisplay.h"
#include "Workspace.h"

#include "Core/Fs/FileTree.h"

#include <imgui.h>

#include <array>
#include <filesystem>
#include <string>

namespace core::text
{
    class DocumentSet;
}

namespace graphite
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
        FileTreeView(Workspace& workspace, const core::text::DocumentSet& documents);

        void Render();

        void SetIgnoredDisplay(IgnoredDisplay display) noexcept { m_ignoredDisplay = display; }

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
        void RenderNode(core::FileNode& node);
        void RenderContextMenu(const core::FileNode& node);
        void RenderDeleteConfirmation();
        [[nodiscard]] bool RenderRenameEditor(const core::FileNode& node);
        void RecordAction(PendingAction action);
        void ExecutePendingAction();
        void HandleDelete(const PendingAction& action);
        void HandleRefresh(const PendingAction& action);
        void HandleRename(const PendingAction& action);
        void HandleDuplicate(const PendingAction& action);
        void HandleCreate(const PendingAction& action);
        [[nodiscard]] core::FileNode* FindNode(const std::filesystem::path& path);
        void RefreshParentOf(const std::filesystem::path& path);
        [[nodiscard]] bool PassesFilter(const core::FileNode& node) const;

        Workspace& m_workspace;
        const core::text::DocumentSet& m_documents;
        ImGuiTextFilter m_filter;
        PendingAction m_pending;
        std::filesystem::path m_deleteCandidate;
        bool m_openDeleteConfirmation = false;

        /// Set by an operation that creates something inside a folder, so the next frame
        /// can force that folder open. node.expanded alone cannot do it: it reaches ImGui
        /// as DefaultOpen, which ImGui ignores once the node has storage.
        std::filesystem::path m_forceOpenPath;

        /// Keyed by path rather than by a pointer to the node: a refresh can drop the
        /// node being edited, and a stale pointer would be written through.
        std::filesystem::path m_renamingPath;

        /// 1024 bytes, not MAX_PATH: a 255-character name can reach roughly 1020 bytes
        /// in UTF-8, and a truncated name would be committed to disk by the blur path.
        std::array<char, 1024> m_renameBuffer{};
        bool m_focusRenameField = false;
        IgnoredDisplay m_ignoredDisplay = IgnoredDisplay::Dim;
    };
}
