#pragma once

#include "Workspace/Workspace.h"

#include "Core/Base/Result.h"
#include "Sln/SolutionModel.h"

#include <imgui.h>

#include <array>
#include <filesystem>
#include <string>

namespace Graphite
{
    /// Draws the solution the workspace has loaded: solution folders, projects, and — once
    /// a project reader fills them in — the folders and files inside each project.
    ///
    /// The tree is read-only, but its context menu edits the solution file through
    /// SolutionEditor: adding and removing projects, and organising solution
    /// folders. Nothing here reads or writes a project file.
    class SolutionTreeView final
    {
    public:
        explicit SolutionTreeView(Workspace& workspace);

        void Render();

    private:
        /// A menu command recorded during the tree walk and executed afterwards. Acting
        /// immediately would edit the solution the walk is reading, and the walk holds
        /// references into it.
        struct PendingAction
        {
            enum class Kind
            {
                None,
                AddProject,
                RemoveProject,
                AddFolder,
                RemoveFolder,
                RenameFolder,
                MoveProject
            };

            Kind kind = Kind::None;
            std::filesystem::path project;
            std::string folder;
            std::string name;
        };

        void RenderFolder(const Folder& folder, const std::string& path);
        void RenderProject(const Project& project, const std::string& folder);
        void RenderContentFolder(const ContentFolder& folder);
        void RenderEmptyState();

        void RenderContextMenu(const Folder& folder, const std::string& path);
        void RenderContextMenu(const Project& project, const std::string& folder);
        [[nodiscard]] bool RenderFolderRenameEditor(const Folder& folder,
                                                    const std::string& path);
        void RenderMoveToFolderMenu(const Project& project,
                                    const std::string& currentFolder);
        void RenderRemoveConfirmation();
        void ExecutePendingAction();
        [[nodiscard]] Core::Result<void> Apply(const PendingAction& action);

        Workspace& m_Workspace;
        ImGuiTextFilter m_Filter;

        PendingAction m_Pending;
        std::filesystem::path m_RemoveCandidate;
        std::string m_RemoveCandidateName;
        bool m_OpenRemoveConfirmation = false;
        std::array<char, 256> m_RenameBuffer{};
        std::string m_RenamingFolder;
        bool m_FocusRenameField = false;
    };
}
