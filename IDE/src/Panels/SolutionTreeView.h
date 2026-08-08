#pragma once

#include "Workspace.h"

#include "Core/Sln/SolutionModel.h"

#include <imgui.h>

namespace ide
{
    /// Draws the solution the workspace has loaded: solution folders, projects, and — once
    /// a project reader fills them in — the folders and files inside each project.
    ///
    /// Read-only. Nothing here writes a solution or project file.
    class SolutionTreeView final
    {
    public:
        explicit SolutionTreeView(Workspace& workspace);

        void Render();

    private:
        void RenderFolder(const core::sln::Folder& folder);
        void RenderProject(const core::sln::Project& project);
        void RenderContentFolder(const core::sln::ContentFolder& folder);
        void RenderEmptyState();

        Workspace& m_workspace;
        ImGuiTextFilter m_filter;
    };
}
