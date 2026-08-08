#include "Panels/SolutionTreeView.h"

#include "Core/Base/Log.h"
#include "Core/Fs/FileSystem.h"
#include "Core/Gui/Icons.h"
#include "Core/Platform/FileDialog.h"
#include "Core/Sln/SolutionEditor.h"

#include <IconsMaterialDesignIcons.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace graphite
{
    namespace
    {
        /// Every logical folder path under `folder`, `path` deep so far. The root itself
        /// is not included — callers that want it add the empty string themselves.
        void CollectFolderPaths(const core::sln::Folder& folder, const std::string& path,
                                std::vector<std::string>& out)
        {
            for (const core::sln::Folder& child : folder.folders)
            {
                const std::string childPath = path.empty() ? child.name : path + "/" + child.name;
                out.push_back(childPath);
                CollectFolderPaths(child, childPath, out);
            }
        }
    }

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

            const bool rootOpen = ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

            // Attached right after the item, regardless of whether it is expanded — a
            // right click on a collapsed root must still open the menu.
            RenderContextMenu(solution.root, "");

            if (rootOpen)
            {
                RenderFolder(solution.root, "");
                ImGui::TreePop();
            }
        }

        ImGui::EndChild();

        // Both run outside the walk: the executor edits the solution the walk just read,
        // and the modal must not be nested inside the child window.
        ExecutePendingAction();
        RenderRemoveConfirmation();
    }

    void SolutionTreeView::RenderFolder(const core::sln::Folder& folder, const std::string& path)
    {
        for (const core::sln::Folder& child : folder.folders)
        {
            const std::string childPath = path.empty() ? child.name : path + "/" + child.name;

            if (RenderFolderRenameEditor(child, childPath))
                continue;

            const std::string label = std::string{ICON_MDI_FOLDER} + "  " + child.name;

            const bool open =
                ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

            RenderContextMenu(child, childPath);

            if (open)
            {
                RenderFolder(child, childPath);
                ImGui::TreePop();
            }
        }

        for (const core::sln::Project& project : folder.projects)
            RenderProject(project, path);
    }

    void SolutionTreeView::RenderProject(const core::sln::Project& project,
                                         const std::string& folder)
    {
        // A project nobody could read is a leaf rather than an empty expandable node: the
        // arrow would promise contents that are not merely absent but unknown.
        const ImGuiTreeNodeFlags flags =
            project.contentRead ? ImGuiTreeNodeFlags_None : ImGuiTreeNodeFlags_Leaf;

        // Said in the label rather than only in a colour: this is the one state in the
        // view the user has to act on, and it is why the solution is worth looking at.
        const std::string icon =
            core::IconForExtension(core::fs::ToUtf8(project.path.extension()));
        const std::string label =
            icon + "  " + (project.exists ? project.displayName
                                           : project.displayName + "  (missing)");

        if (!project.exists)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

        const bool open = ImGui::TreeNodeEx(label.c_str(), flags);

        if (!project.exists)
            ImGui::PopStyleColor();

        // Attached right after the item, regardless of whether it is expanded.
        RenderContextMenu(project, folder);

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
            const std::string label = std::string{ICON_MDI_FOLDER} + "  " + child.name;

            if (ImGui::TreeNodeEx(label.c_str()))
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

            const std::string label =
                std::string{core::IconForExtension(core::fs::ToUtf8(file.path.extension()))} +
                "  " + file.name;

            // Keyed by the full path: two projects can hold files of the same name, and
            // ImGui would treat same-labelled siblings as one node.
            ImGui::PushID(core::fs::ToUtf8(file.path).c_str());

            if (ImGui::Selectable(label.c_str(), selected))
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

    void SolutionTreeView::RenderContextMenu(const core::sln::Folder& folder,
                                             const std::string& path)
    {
        // BeginPopupContextItem cannot pass window flags, and without NoMove ImGui lets
        // the menu be dragged anywhere by its body — a popup has no title bar, so its
        // whole surface counts as draggable background. So open the popup by hand and use
        // BeginPopup, which does take window flags. Mirrors FileTreeView::RenderContextMenu.
        const std::string popupId = "##ctxSlnFolder" + path;

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
        {
            ImGui::OpenPopup(popupId.c_str());
        }

        if (!ImGui::BeginPopup(popupId.c_str(), ImGuiWindowFlags_NoMove))
            return;

        // The solution root is not itself a folder the editor can rename or remove.
        const bool isRoot = path.empty();

        if (ImGui::MenuItem("Add Existing Project..."))
            m_pending = PendingAction{PendingAction::Kind::AddProject, {}, path, {}};

        if (ImGui::MenuItem("New Folder"))
        {
            const std::string childPath = path.empty() ? "New Folder" : path + "/New Folder";
            m_pending = PendingAction{PendingAction::Kind::AddFolder, {}, childPath, {}};
        }

        if (!isRoot)
        {
            ImGui::Separator();

            if (ImGui::MenuItem("Rename"))
            {
                m_renamingFolder = path;
                m_focusRenameField = true;

                std::fill(m_renameBuffer.begin(), m_renameBuffer.end(), '\0');

                if (folder.name.size() < m_renameBuffer.size())
                    std::copy_n(folder.name.begin(), folder.name.size(), m_renameBuffer.begin());
            }

            if (ImGui::MenuItem("Remove"))
                m_pending = PendingAction{PendingAction::Kind::RemoveFolder, {}, path, {}};
        }

        ImGui::EndPopup();
    }

    void SolutionTreeView::RenderContextMenu(const core::sln::Project& project,
                                             const std::string& folder)
    {
        const std::string popupId = "##ctxSlnProject" + core::fs::ToUtf8(project.path);

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
        {
            ImGui::OpenPopup(popupId.c_str());
        }

        if (!ImGui::BeginPopup(popupId.c_str(), ImGuiWindowFlags_NoMove))
            return;

        if (ImGui::MenuItem("Remove from Solution"))
        {
            m_removeCandidate = project.path;
            m_removeCandidateName = project.displayName;
            m_openRemoveConfirmation = true;
        }

        if (ImGui::BeginMenu("Move to Folder"))
        {
            RenderMoveToFolderMenu(project, folder);
            ImGui::EndMenu();
        }

        ImGui::EndPopup();
    }

    void SolutionTreeView::RenderMoveToFolderMenu(const core::sln::Project& project,
                                                  const std::string& currentFolder)
    {
        if (!m_workspace.LoadedSolution())
            return;

        std::vector<std::string> folders{""};
        CollectFolderPaths(m_workspace.LoadedSolution()->root, "", folders);

        for (const std::string& path : folders)
        {
            // Already there — offering it as a destination would be a no-op move.
            if (path == currentFolder)
                continue;

            const std::string label = path.empty() ? "(Solution Root)" : path;

            if (ImGui::MenuItem(label.c_str()))
                m_pending = PendingAction{PendingAction::Kind::MoveProject, project.path, path, {}};
        }
    }

    bool SolutionTreeView::RenderFolderRenameEditor(const core::sln::Folder& folder,
                                                    const std::string& path)
    {
        if (m_renamingFolder != path)
            return false;

        if (m_focusRenameField)
        {
            ImGui::SetKeyboardFocusHere();
            m_focusRenameField = false;
        }

        ImGui::SetNextItemWidth(200.0f);

        const bool committed =
            ImGui::InputText("##renameSlnFolder", m_renameBuffer.data(), m_renameBuffer.size(),
                             ImGuiInputTextFlags_EnterReturnsTrue |
                                 ImGuiInputTextFlags_AutoSelectAll);

        // Escape cancels, but only while this field owns the keyboard — an unscoped key
        // poll would also catch an Escape aimed at a modal elsewhere.
        const bool cancelled = ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape);
        const bool deactivated = !cancelled && ImGui::IsItemDeactivated();

        if (cancelled)
        {
            m_renamingFolder.clear();
        }
        else if (committed || deactivated)
        {
            const std::string typed = m_renameBuffer.data();

            if (!typed.empty() && typed != folder.name)
                m_pending = PendingAction{PendingAction::Kind::RenameFolder, {}, path, typed};

            m_renamingFolder.clear();
        }

        return true;
    }

    void SolutionTreeView::RenderRemoveConfirmation()
    {
        if (m_openRemoveConfirmation)
        {
            ImGui::OpenPopup("Remove from Solution##solutionTreeRemove");
            m_openRemoveConfirmation = false;
        }

        if (!ImGui::BeginPopupModal("Remove from Solution##solutionTreeRemove", nullptr,
                                    ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }

        ImGui::Text("Remove \"%s\" from the solution?", m_removeCandidateName.c_str());
        ImGui::TextDisabled("Its files stay on disk — only the solution's reference is removed.");
        ImGui::Spacing();

        if (ImGui::Button("Remove", ImVec2(90.0f, 0.0f)))
        {
            const PendingAction action{PendingAction::Kind::RemoveProject, m_removeCandidate, {}, {}};

            if (const auto applied = Apply(action); !applied)
                CORE_ERROR("{}", applied.GetError().message);

            m_removeCandidate.clear();
            m_removeCandidateName.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f)))
        {
            m_removeCandidate.clear();
            m_removeCandidateName.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void SolutionTreeView::ExecutePendingAction()
    {
        if (m_pending.kind == PendingAction::Kind::None)
            return;

        PendingAction action = std::move(m_pending);
        m_pending = PendingAction{};

        // The picker is shown here rather than from the menu that recorded the action: a
        // system dialog opened from inside the tree walk would be modal over a window
        // whose draw is not finished, and the walk holds references into the solution
        // this action is about to invalidate.
        if (action.kind == PendingAction::Kind::AddProject)
        {
            const std::vector<core::FileDialog::Filter> filters = {
                {"Visual Studio projects", "*.vcxproj"}, {"All files", "*.*"}};

            auto picked = core::FileDialog::PickFile("Add Existing Project", filters);

            if (!picked)
            {
                CORE_ERROR("Cannot open the file picker: {}", picked.GetError().message);
                return;
            }

            // An empty path is the user cancelling, which is not worth a log line.
            if (picked.Value().empty())
                return;

            action.project = picked.Value();
        }

        // No reload here: writing the file changes something the directory watcher is
        // already watching, and the reload arrives through that path. Reloading here too
        // would give the view two ways to update, and one of them would eventually be
        // wrong.
        if (const auto applied = Apply(action); !applied)
            CORE_ERROR("{}", applied.GetError().message);
    }

    core::Result<void> SolutionTreeView::Apply(const PendingAction& action)
    {
        if (!m_workspace.LoadedSolution())
            return core::Error{core::ErrorCode::NotFound, "no solution is open"};

        auto editor = core::sln::SolutionEditor::Open(m_workspace.LoadedSolution()->path);

        if (!editor)
            return editor.GetError();

        core::Result<void> applied = core::Result<void>::Ok();

        switch (action.kind)
        {
        case PendingAction::Kind::AddProject:
            applied = editor.Value()->AddProject(action.project, action.folder);
            break;

        case PendingAction::Kind::RemoveProject:
            applied = editor.Value()->RemoveProject(action.project);
            break;

        case PendingAction::Kind::AddFolder:
            applied = editor.Value()->AddFolder(action.folder);
            break;

        case PendingAction::Kind::RemoveFolder:
            applied = editor.Value()->RemoveFolder(action.folder);
            break;

        case PendingAction::Kind::RenameFolder:
            applied = editor.Value()->RenameFolder(action.folder, action.name);
            break;

        case PendingAction::Kind::MoveProject:
            applied = editor.Value()->MoveProject(action.project, action.folder);
            break;

        case PendingAction::Kind::None:
            return core::Result<void>::Ok();
        }

        if (!applied)
            return applied;

        return editor.Value()->Save();
    }
}
