#pragma once

#include "Workspace.h"

#include "Core/Base/Result.h"
#include "Core/Sln/SolutionModel.h"

#include <imgui.h>

#include <array>
#include <filesystem>
#include <string>

namespace graphite
{
    /// Draws the solution the workspace has loaded: solution folders, projects, and — once
    /// a project reader fills them in — the folders and files inside each project.
    ///
    /// The tree is read-only, but its context menu edits the solution file through
    /// core::sln::SolutionEditor: adding and removing projects, and organising solution
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

        void RenderFolder(const core::sln::Folder& folder, const std::string& path);
        void RenderProject(const core::sln::Project& project, const std::string& folder);
        void RenderContentFolder(const core::sln::ContentFolder& folder);
        void RenderEmptyState();

        void RenderContextMenu(const core::sln::Folder& folder, const std::string& path);
        void RenderContextMenu(const core::sln::Project& project, const std::string& folder);
        [[nodiscard]] bool RenderFolderRenameEditor(const core::sln::Folder& folder,
                                                    const std::string& path);
        void RenderMoveToFolderMenu(const core::sln::Project& project,
                                    const std::string& currentFolder);
        void RenderRemoveConfirmation();
        void ExecutePendingAction();
        [[nodiscard]] core::Result<void> Apply(const PendingAction& action);

        Workspace& m_workspace;
        ImGuiTextFilter m_filter;

        PendingAction m_pending;
        std::filesystem::path m_removeCandidate;
        std::string m_removeCandidateName;
        bool m_openRemoveConfirmation = false;
        std::array<char, 256> m_renameBuffer{};
        std::string m_renamingFolder;
        bool m_focusRenameField = false;
    };
}
