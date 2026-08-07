#pragma once

#include "Workspace.h"

#include "Core/Fs/FileTree.h"
#include "Core/Ui/Panel.h"

#include <imgui.h>

#include <filesystem>
#include <string>

namespace ide
{
    /// Browses the open workspace folder. Holds no state of its own beyond the filter —
    /// the tree lives in the Workspace.
    class FileTreePanel final : public core::Panel
    {
    public:
        explicit FileTreePanel(Workspace& workspace);

        void OnUIRender() override;

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
        void ExecutePendingAction();
        [[nodiscard]] core::FileNode* FindNode(const std::filesystem::path& path);
        void RefreshParentOf(const std::filesystem::path& path);
        [[nodiscard]] bool PassesFilter(const core::FileNode& node) const;

        Workspace& m_workspace;
        ImGuiTextFilter m_filter;
        PendingAction m_pending;
        std::filesystem::path m_deleteCandidate;
        bool m_openDeleteConfirmation = false;
    };
}
