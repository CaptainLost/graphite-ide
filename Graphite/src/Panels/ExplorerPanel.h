#pragma once

#include "GitIgnoredDisplay.h"
#include "Panels/FileTreeView.h"
#include "Panels/SolutionTreeView.h"
#include "Workspace.h"

#include "Core/Ui/Panel.h"

namespace core
{
    class CommandRegistry;
}

namespace core::text
{
    class DocumentSet;
}

namespace graphite
{
    /// The left-hand panel. Owns the two ways of looking at a workspace and switches
    /// between them, drawing nothing itself beyond that choice.
    ///
    /// Registered under the id "files", unchanged from when it was the file tree, so
    /// docking layouts saved in imgui.ini keep working.
    class ExplorerPanel final : public core::Panel
    {
    public:
        /// `documents` is passed straight through to the file tree, which marks the open
        /// files and the one on screen. Read there, never modified.
        ExplorerPanel(Workspace& workspace, const core::text::DocumentSet& documents);

        void OnUIRender() override;

        /// Reads the stored display mode, keeping the default when nothing was stored.
        void LoadSettings();

        void RegisterCommands(core::CommandRegistry& commands);

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

        /// The single path all three modes go through. `persist` is false only for the
        /// load at startup, which would otherwise write back what it just read.
        void Apply(GitIgnoredDisplay display, bool persist);

        Workspace& m_workspace;
        FileTreeView m_files;
        SolutionTreeView m_solution;
        Mode m_mode = Mode::Files;

        /// The root the current mode was loaded for. Empty until the first load.
        std::filesystem::path m_modeLoadedFor;

        GitIgnoredDisplay m_display = GitIgnoredDisplay::Dim;
    };
}
