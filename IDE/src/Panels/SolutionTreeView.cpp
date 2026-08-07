#include "Panels/SolutionTreeView.h"

#include "Core/Fs/FileSystem.h"

#include <string>

namespace ide
{
    SolutionTreeView::SolutionTreeView(Workspace& workspace) : m_workspace(workspace)
    {
    }

    void SolutionTreeView::Render()
    {
        if (!m_workspace.IsOpen() || !m_workspace.LoadedSolution())
        {
            RenderEmptyState();
            return;
        }

        ImGui::SetNextItemWidth(-1.0f);
        m_filter.Draw("##solutionFilter");
        ImGui::Separator();

        const core::sln::Solution& solution = *m_workspace.LoadedSolution();

        if (ImGui::BeginChild("##solutionBody", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                              ImGuiWindowFlags_HorizontalScrollbar))
        {
            const std::string label =
                solution.name + " (" + std::to_string(solution.projectCount) + " projects)";

            if (ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                RenderFolder(solution.root);
                ImGui::TreePop();
            }
        }

        ImGui::EndChild();
    }

    void SolutionTreeView::RenderFolder(const core::sln::Folder& folder)
    {
        for (const core::sln::Folder& child : folder.folders)
        {
            if (ImGui::TreeNodeEx(child.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                RenderFolder(child);
                ImGui::TreePop();
            }
        }

        for (const core::sln::Project& project : folder.projects)
            RenderProject(project);
    }

    void SolutionTreeView::RenderProject(const core::sln::Project& project)
    {
        // A project nobody could read is a leaf rather than an empty expandable node: the
        // arrow would promise contents that are not merely absent but unknown.
        const ImGuiTreeNodeFlags flags =
            project.contentRead ? ImGuiTreeNodeFlags_None : ImGuiTreeNodeFlags_Leaf;

        // Said in the label rather than only in a colour: this is the one state in the
        // view the user has to act on, and it is why the solution is worth looking at.
        const std::string label =
            project.exists ? project.displayName : project.displayName + "  (missing)";

        if (!project.exists)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

        const bool open = ImGui::TreeNodeEx(label.c_str(), flags);

        if (!project.exists)
            ImGui::PopStyleColor();

        if (!open)
            return;

        if (project.contentRead)
        {
            if (!project.content.references.empty() && ImGui::TreeNodeEx("References"))
            {
                for (const std::string& reference : project.content.references)
                    ImGui::BulletText("%s", reference.c_str());

                ImGui::TreePop();
            }

            RenderContentFolder(project.content.root);
        }

        ImGui::TreePop();
    }

    void SolutionTreeView::RenderContentFolder(const core::sln::ContentFolder& folder)
    {
        for (const core::sln::ContentFolder& child : folder.folders)
        {
            if (ImGui::TreeNodeEx(child.name.c_str()))
            {
                RenderContentFolder(child);
                ImGui::TreePop();
            }
        }

        for (const core::sln::ContentFile& file : folder.files)
        {
            if (!m_filter.PassFilter(file.name.c_str()))
                continue;

            const bool selected = m_workspace.SelectedFile() == file.path;

            // Keyed by the full path: two projects can hold files of the same name, and
            // ImGui would treat same-labelled siblings as one node.
            ImGui::PushID(core::fs::ToUtf8(file.path).c_str());

            if (ImGui::Selectable(file.name.c_str(), selected))
                m_workspace.SelectFile(file.path);

            ImGui::PopID();
        }
    }

    void SolutionTreeView::RenderEmptyState()
    {
        if (!m_workspace.IsOpen())
        {
            ImGui::TextUnformatted("Open a folder to see its Visual Studio solution.");
            return;
        }

        if (m_workspace.SolutionCandidates().empty())
        {
            ImGui::TextUnformatted("No Visual Studio solution in this folder.");
            return;
        }

        // Candidates exist but none loaded, so reading the chosen one failed. The reason
        // was logged to the console when it happened.
        ImGui::TextUnformatted("This Visual Studio solution could not be read. See the console.");
    }
}
