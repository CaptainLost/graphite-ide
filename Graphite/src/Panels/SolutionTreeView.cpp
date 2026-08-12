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

namespace Graphite
{
    namespace
    {
        /// Every logical folder path under `folder`, `path` deep so far. The root itself
        /// is not included — callers that want it add the empty string themselves.
        void CollectFolderPaths(const Core::Folder& folder, const std::string& path,
                                std::vector<std::string>& out)
        {
            for (const Core::Folder& child : folder.folders)
            {
                const std::string childPath = path.empty() ? child.name : path + "/" + child.name;
                out.push_back(childPath);
                CollectFolderPaths(child, childPath, out);
            }
        }
    }

    SolutionTreeView::SolutionTreeView(Workspace& workspace) : m_Workspace(workspace)
    {
    }

    void SolutionTreeView::Render()
    {
        if (!m_Workspace.IsOpen() || !m_Workspace.LoadedSolution())
        {
            RenderEmptyState();
            return;
        }

        ImGui::SetNextItemWidth(-1.0f);

        if (ImGui::InputTextWithHint("##solutionFilter", ICON_MDI_MAGNIFY "  Filter files",
                                     m_Filter.InputBuf, IM_ARRAYSIZE(m_Filter.InputBuf)))
        {
            m_Filter.Build();
        }

        ImGui::Separator();

        const Core::Solution& solution = *m_Workspace.LoadedSolution();

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

    void SolutionTreeView::RenderFolder(const Core::Folder& folder, const std::string& path)
    {
        for (const Core::Folder& child : folder.folders)
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

        for (const Core::Project& project : folder.projects)
            RenderProject(project, path);
    }

    void SolutionTreeView::RenderProject(const Core::Project& project,
                                         const std::string& folder)
    {
        // A project nobody could read is a leaf rather than an empty expandable node: the
        // arrow would promise contents that are not merely absent but unknown.
        const ImGuiTreeNodeFlags flags =
            project.contentRead ? ImGuiTreeNodeFlags_None : ImGuiTreeNodeFlags_Leaf;

        // Said in the label rather than only in a colour: this is the one state in the
        // view the user has to act on, and it is why the solution is worth looking at.
        const std::string icon =
            Core::IconForExtension(Core::ToUtf8(project.path.extension()));
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

    void SolutionTreeView::RenderContentFolder(const Core::ContentFolder& folder)
    {
        for (const Core::ContentFolder& child : folder.folders)
        {
            const std::string label = std::string{ICON_MDI_FOLDER} + "  " + child.name;

            if (ImGui::TreeNodeEx(label.c_str()))
            {
                RenderContentFolder(child);
                ImGui::TreePop();
            }
        }

        for (const Core::ContentFile& file : folder.files)
        {
            if (!m_Filter.PassFilter(file.name.c_str()))
                continue;

            const bool selected = m_Workspace.GetSelectedFile() == file.path;

            const std::string label =
                std::string{Core::IconForExtension(Core::ToUtf8(file.path.extension()))} +
                "  " + file.name;

            // Keyed by the full path: two projects can hold files of the same name, and
            // ImGui would treat same-labelled siblings as one node.
            ImGui::PushID(Core::ToUtf8(file.path).c_str());

            if (ImGui::Selectable(label.c_str(), selected))
                m_Workspace.SelectFile(file.path);

            ImGui::PopID();
        }
    }

    void SolutionTreeView::RenderEmptyState()
    {
        if (!m_Workspace.IsOpen())
        {
            ImGui::TextUnformatted("Open a folder to see its Visual Studio solution.");
            return;
        }

        if (m_Workspace.GetSolutionCandidates().empty())
        {
            ImGui::TextUnformatted("No Visual Studio solution in this folder.");
            return;
        }

        // Candidates exist but none loaded, so reading the chosen one failed. The reason
        // was logged to the console when it happened.
        ImGui::TextUnformatted("This Visual Studio solution could not be read. See the console.");
    }

    void SolutionTreeView::RenderContextMenu(const Core::Folder& folder,
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
            m_Pending = PendingAction{PendingAction::Kind::AddProject, {}, path, {}};

        if (ImGui::MenuItem("New Folder"))
        {
            const std::string childPath = path.empty() ? "New Folder" : path + "/New Folder";
            m_Pending = PendingAction{PendingAction::Kind::AddFolder, {}, childPath, {}};
        }

        if (!isRoot)
        {
            ImGui::Separator();

            if (ImGui::MenuItem("Rename"))
            {
                m_RenamingFolder = path;
                m_FocusRenameField = true;

                std::fill(m_RenameBuffer.begin(), m_RenameBuffer.end(), '\0');

                if (folder.name.size() < m_RenameBuffer.size())
                    std::copy_n(folder.name.begin(), folder.name.size(), m_RenameBuffer.begin());
            }

            if (ImGui::MenuItem("Remove"))
                m_Pending = PendingAction{PendingAction::Kind::RemoveFolder, {}, path, {}};
        }

        ImGui::EndPopup();
    }

    void SolutionTreeView::RenderContextMenu(const Core::Project& project,
                                             const std::string& folder)
    {
        const std::string popupId = "##ctxSlnProject" + Core::ToUtf8(project.path);

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
        {
            ImGui::OpenPopup(popupId.c_str());
        }

        if (!ImGui::BeginPopup(popupId.c_str(), ImGuiWindowFlags_NoMove))
            return;

        if (ImGui::MenuItem("Remove from Solution"))
        {
            m_RemoveCandidate = project.path;
            m_RemoveCandidateName = project.displayName;
            m_OpenRemoveConfirmation = true;
        }

        if (ImGui::BeginMenu("Move to Folder"))
        {
            RenderMoveToFolderMenu(project, folder);
            ImGui::EndMenu();
        }

        ImGui::EndPopup();
    }

    void SolutionTreeView::RenderMoveToFolderMenu(const Core::Project& project,
                                                  const std::string& currentFolder)
    {
        if (!m_Workspace.LoadedSolution())
            return;

        std::vector<std::string> folders{""};
        CollectFolderPaths(m_Workspace.LoadedSolution()->root, "", folders);

        for (const std::string& path : folders)
        {
            // Already there — offering it as a destination would be a no-op move.
            if (path == currentFolder)
                continue;

            const std::string label = path.empty() ? "(Solution Root)" : path;

            if (ImGui::MenuItem(label.c_str()))
                m_Pending = PendingAction{PendingAction::Kind::MoveProject, project.path, path, {}};
        }
    }

    bool SolutionTreeView::RenderFolderRenameEditor(const Core::Folder& folder,
                                                    const std::string& path)
    {
        if (m_RenamingFolder != path)
            return false;

        if (m_FocusRenameField)
        {
            ImGui::SetKeyboardFocusHere();
            m_FocusRenameField = false;
        }

        ImGui::SetNextItemWidth(200.0f);

        const bool committed =
            ImGui::InputText("##renameSlnFolder", m_RenameBuffer.data(), m_RenameBuffer.size(),
                             ImGuiInputTextFlags_EnterReturnsTrue |
                                 ImGuiInputTextFlags_AutoSelectAll);

        // Escape cancels, but only while this field owns the keyboard — an unscoped key
        // poll would also catch an Escape aimed at a modal elsewhere.
        const bool cancelled = ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape);
        const bool deactivated = !cancelled && ImGui::IsItemDeactivated();

        if (cancelled)
        {
            m_RenamingFolder.clear();
        }
        else if (committed || deactivated)
        {
            const std::string typed = m_RenameBuffer.data();

            if (!typed.empty() && typed != folder.name)
                m_Pending = PendingAction{PendingAction::Kind::RenameFolder, {}, path, typed};

            m_RenamingFolder.clear();
        }

        return true;
    }

    void SolutionTreeView::RenderRemoveConfirmation()
    {
        if (m_OpenRemoveConfirmation)
        {
            ImGui::OpenPopup("Remove from Solution##solutionTreeRemove");
            m_OpenRemoveConfirmation = false;
        }

        if (!ImGui::BeginPopupModal("Remove from Solution##solutionTreeRemove", nullptr,
                                    ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }

        ImGui::Text("Remove \"%s\" from the solution?", m_RemoveCandidateName.c_str());
        ImGui::TextDisabled("Its files stay on disk — only the solution's reference is removed.");
        ImGui::Spacing();

        if (ImGui::Button("Remove", ImVec2(90.0f, 0.0f)))
        {
            const PendingAction action{PendingAction::Kind::RemoveProject, m_RemoveCandidate, {}, {}};

            if (const auto applied = Apply(action); !applied)
                CORE_ERROR("{}", applied.GetError().message);

            m_RemoveCandidate.clear();
            m_RemoveCandidateName.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f)))
        {
            m_RemoveCandidate.clear();
            m_RemoveCandidateName.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void SolutionTreeView::ExecutePendingAction()
    {
        if (m_Pending.kind == PendingAction::Kind::None)
            return;

        PendingAction action = std::move(m_Pending);
        m_Pending = PendingAction{};

        // The picker is shown here rather than from the menu that recorded the action: a
        // system dialog opened from inside the tree walk would be modal over a window
        // whose draw is not finished, and the walk holds references into the solution
        // this action is about to invalidate.
        if (action.kind == PendingAction::Kind::AddProject)
        {
            const std::vector<Core::FileDialog::Filter> filters = {
                {"Visual Studio projects", "*.vcxproj"}, {"All files", "*.*"}};

            auto picked = Core::FileDialog::PickFile("Add Existing Project", filters);

            if (!picked)
            {
                CORE_ERROR("Cannot open the file picker: {}", picked.GetError().message);
                return;
            }

            // An empty path is the user cancelling, which is not worth a log line.
            if (picked.GetValue().empty())
                return;

            action.project = picked.GetValue();
        }

        // No reload here: writing the file changes something the directory watcher is
        // already watching, and the reload arrives through that path. Reloading here too
        // would give the view two ways to update, and one of them would eventually be
        // wrong.
        if (const auto applied = Apply(action); !applied)
            CORE_ERROR("{}", applied.GetError().message);
    }

    Core::Result<void> SolutionTreeView::Apply(const PendingAction& action)
    {
        if (!m_Workspace.LoadedSolution())
            return Core::Error{Core::ErrorCode::NotFound, "no solution is open"};

        auto editor = Core::SolutionEditor::Open(m_Workspace.LoadedSolution()->path);

        if (!editor)
            return editor.GetError();

        Core::Result<void> applied = Core::Result<void>::Ok();

        switch (action.kind)
        {
        case PendingAction::Kind::AddProject:
            applied = editor.GetValue()->AddProject(action.project, action.folder);
            break;

        case PendingAction::Kind::RemoveProject:
            applied = editor.GetValue()->RemoveProject(action.project);
            break;

        case PendingAction::Kind::AddFolder:
            applied = editor.GetValue()->AddFolder(action.folder);
            break;

        case PendingAction::Kind::RemoveFolder:
            applied = editor.GetValue()->RemoveFolder(action.folder);
            break;

        case PendingAction::Kind::RenameFolder:
            applied = editor.GetValue()->RenameFolder(action.folder, action.name);
            break;

        case PendingAction::Kind::MoveProject:
            applied = editor.GetValue()->MoveProject(action.project, action.folder);
            break;

        case PendingAction::Kind::None:
            return Core::Result<void>::Ok();
        }

        if (!applied)
            return applied;

        return editor.GetValue()->Save();
    }
}
