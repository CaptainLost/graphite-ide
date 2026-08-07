#pragma once

#include "IgnoredDisplay.h"
#include "Panels/FileTreeView.h"
#include "Panels/SolutionTreeView.h"
#include "Workspace.h"

#include "Core/Ui/Panel.h"

namespace ide
{
    /// The left-hand panel. Owns the two ways of looking at a workspace and switches
    /// between them, drawing nothing itself beyond that choice.
    ///
    /// Registered under the id "files", unchanged from when it was the file tree, so
    /// docking layouts saved in imgui.ini keep working.
    class ExplorerPanel final : public core::Panel
    {
    public:
        explicit ExplorerPanel(Workspace& workspace);

        void OnUIRender() override;

        void SetIgnoredDisplay(IgnoredDisplay display) noexcept
        {
            m_files.SetIgnoredDisplay(display);
        }

    private:
        enum class Mode
        {
            Files,
            Solution
        };

        void RenderHeader();
        void SetMode(Mode mode);

        /// Reloads the remembered mode when the open folder changes. Done by watching the
        /// root rather than by subscribing to WorkspaceOpenedEvent, because this covers
        /// opening, closing and the folder being moved on disk with one comparison.
        void SyncModeWithWorkspace();

        Workspace& m_workspace;
        FileTreeView m_files;
        SolutionTreeView m_solution;
        Mode m_mode = Mode::Files;

        /// The root the current mode was loaded for. Empty until the first load.
        std::filesystem::path m_modeLoadedFor;
    };
}
