#pragma once

#include "Workspace.h"

#include "Core/Fs/FileTree.h"
#include "Core/Ui/Panel.h"

#include <imgui.h>

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
        void RenderToolbar();
        void RenderEmptyState();
        void RenderNode(core::FileNode& node);
        void RenderContextMenu(const core::FileNode& node);
        [[nodiscard]] bool PassesFilter(const core::FileNode& node) const;

        Workspace& m_workspace;
        ImGuiTextFilter m_filter;
    };
}
