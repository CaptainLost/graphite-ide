#include "Panels/SolutionTreeView.h"

#include "Core/Base/Log.h"
#include "Core/Fs/FileSystem.h"
#include "Core/ImGui/Icons.h"
#include "Core/Platform/FileDialog.h"
#include "Sln/SolutionEditor.h"

#include <IconsMaterialDesignIcons.h>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {
		std::string ChildPath(const std::string& path, const std::string& name)
		{
			return path.empty() ? name : path + "/" + name;
		}

		void CollectFolderPaths(const Folder& folder, const std::string& path,
								std::vector<std::string>& out)
		{
			for (const Folder& child : folder.Folders)
			{
				const std::string childPath = ChildPath(path, child.Name);
				out.push_back(childPath);
				CollectFolderPaths(child, childPath, out);
			}
		}

		// BeginPopupContextItem cannot pass window flags, and without NoMove ImGui lets the
		// menu be dragged by its body: a popup has no title bar, so its whole surface counts
		// as draggable background. Opening by hand allows BeginPopup, which does take window
		// flags.
		bool BeginRightClickPopup(const char* popupId)
		{
			if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
				ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
			{
				ImGui::OpenPopup(popupId);
			}

			return ImGui::BeginPopup(popupId, ImGuiWindowFlags_NoMove);
		}

		std::optional<std::filesystem::path> PickProjectFile()
		{
			const std::vector<Core::FileDialog::Filter> filters = {
				{"Visual Studio projects", "*.vcxproj"}, {"All files", "*.*"}};

			auto picked = Core::FileDialog::PickFile("Add Existing Project", filters);

			if (!picked)
			{
				APP_ERROR("Cannot open the file picker: {}", picked.GetError().Message);
				return std::nullopt;
			}

			if (picked.GetValue().empty())
				return std::nullopt;

			return picked.GetValue();
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

		RenderFilter();
		ImGui::Separator();

		const Solution& solution = *m_Workspace.LoadedSolution();

		if (ImGui::BeginChild("##solutionBody", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
							  ImGuiWindowFlags_HorizontalScrollbar))
		{
			const std::string label =
				solution.Name + " (" + std::to_string(solution.ProjectCount) + " projects)";

			const bool rootOpen = ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

			RenderContextMenu(solution.Root, "");

			if (rootOpen)
			{
				RenderFolder(solution.Root, "");
				ImGui::TreePop();
			}
		}

		ImGui::EndChild();

		// Outside the walk: the executor edits the solution the walk holds references into,
		// and the modal must not be nested inside the child window.
		ExecutePendingAction();
		RenderRemoveConfirmation();
	}

	void SolutionTreeView::RenderFilter()
	{
		ImGui::SetNextItemWidth(-1.0f);

		if (ImGui::InputTextWithHint("##solutionFilter", ICON_MDI_MAGNIFY "  Filter files",
									 m_Filter.InputBuf, IM_ARRAYSIZE(m_Filter.InputBuf)))
		{
			m_Filter.Build();
		}
	}

	void SolutionTreeView::RenderFolder(const Folder& folder, const std::string& path)
	{
		for (const Folder& child : folder.Folders)
		{
			const std::string childPath = ChildPath(path, child.Name);

			if (RenderFolderRenameEditor(child, childPath))
				continue;

			const std::string label = std::string{ICON_MDI_FOLDER} + "  " + child.Name;

			const bool open =
				ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

			RenderContextMenu(child, childPath);

			if (open)
			{
				RenderFolder(child, childPath);
				ImGui::TreePop();
			}
		}

		for (const Project& project : folder.Projects)
			RenderProject(project, path);
	}

	void SolutionTreeView::RenderProject(const Project& project,
										 const std::string& folder)
	{
		const ImGuiTreeNodeFlags flags =
			project.ContentRead ? ImGuiTreeNodeFlags_None : ImGuiTreeNodeFlags_Leaf;

		const std::string icon =
			Core::IconForExtension(Core::ToUtf8(project.Path.extension()));
		const std::string label =
			icon + "  " + (project.Exists ? project.DisplayName : project.DisplayName + "  (missing)");

		if (!project.Exists)
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

		const bool open = ImGui::TreeNodeEx(label.c_str(), flags);

		if (!project.Exists)
			ImGui::PopStyleColor();

		RenderContextMenu(project, folder);

		if (!open)
			return;

		if (project.ContentRead)
		{
			RenderReferences(project.Content.References);
			RenderContentFolder(project.Content.Root);
		}

		ImGui::TreePop();
	}

	void SolutionTreeView::RenderReferences(const std::vector<std::string>& references)
	{
		if (references.empty() || !ImGui::TreeNodeEx("References"))
			return;

		for (const std::string& reference : references)
			ImGui::BulletText("%s", reference.c_str());

		ImGui::TreePop();
	}

	void SolutionTreeView::RenderContentFolder(const ContentFolder& folder)
	{
		for (const ContentFolder& child : folder.Folders)
		{
			const std::string label = std::string{ICON_MDI_FOLDER} + "  " + child.Name;

			if (ImGui::TreeNodeEx(label.c_str()))
			{
				RenderContentFolder(child);
				ImGui::TreePop();
			}
		}

		for (const ContentFile& file : folder.Files)
		{
			if (m_Filter.PassFilter(file.Name.c_str()))
				RenderContentFile(file);
		}
	}

	void SolutionTreeView::RenderContentFile(const ContentFile& file)
	{
		const bool selected = m_Workspace.GetSelectedFile() == file.Path;

		const std::string label =
			std::string{Core::IconForExtension(Core::ToUtf8(file.Path.extension()))} + "  " +
			file.Name;

		// Keyed by the full path: ImGui gives same-labelled siblings the same id, and two
		// projects can hold files of the same name.
		ImGui::PushID(Core::ToUtf8(file.Path).c_str());

		if (ImGui::Selectable(label.c_str(), selected))
			m_Workspace.SelectFile(file.Path);

		ImGui::PopID();
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

		ImGui::TextUnformatted("This Visual Studio solution could not be read. See the console.");
	}

	void SolutionTreeView::RenderContextMenu(const Folder& folder,
											 const std::string& path)
	{
		const std::string popupId = "##ctxSlnFolder" + path;

		if (!BeginRightClickPopup(popupId.c_str()))
			return;

		const bool isRoot = path.empty();

		if (ImGui::MenuItem("Add Existing Project..."))
			m_Pending = PendingAction{PendingAction::Kind::AddProject, {}, path, {}};

		if (ImGui::MenuItem("New Folder"))
		{
			m_Pending =
				PendingAction{PendingAction::Kind::AddFolder, {}, ChildPath(path, "New Folder"), {}};
		}

		if (!isRoot)
		{
			ImGui::Separator();

			if (ImGui::MenuItem("Rename"))
				StartRenamingFolder(folder, path);

			if (ImGui::MenuItem("Remove"))
				m_Pending = PendingAction{PendingAction::Kind::RemoveFolder, {}, path, {}};
		}

		ImGui::EndPopup();
	}

	void SolutionTreeView::StartRenamingFolder(const Folder& folder, const std::string& path)
	{
		m_RenamingFolder = path;
		m_FocusRenameField = true;

		std::fill(m_RenameBuffer.begin(), m_RenameBuffer.end(), '\0');

		if (folder.Name.size() < m_RenameBuffer.size())
			std::copy_n(folder.Name.begin(), folder.Name.size(), m_RenameBuffer.begin());
	}

	void SolutionTreeView::RenderContextMenu(const Project& project,
											 const std::string& folder)
	{
		const std::string popupId = "##ctxSlnProject" + Core::ToUtf8(project.Path);

		if (!BeginRightClickPopup(popupId.c_str()))
			return;

		if (ImGui::MenuItem("Remove from Solution"))
		{
			m_RemoveCandidate = project.Path;
			m_RemoveCandidateName = project.DisplayName;
			m_OpenRemoveConfirmation = true;
		}

		if (ImGui::BeginMenu("Move to Folder"))
		{
			RenderMoveToFolderMenu(project, folder);
			ImGui::EndMenu();
		}

		ImGui::EndPopup();
	}

	void SolutionTreeView::RenderMoveToFolderMenu(const Project& project,
												  const std::string& currentFolder)
	{
		if (!m_Workspace.LoadedSolution())
			return;

		std::vector<std::string> folders{""};
		CollectFolderPaths(m_Workspace.LoadedSolution()->Root, "", folders);

		for (const std::string& path : folders)
		{
			if (path == currentFolder)
				continue;

			const std::string label = path.empty() ? "(Solution Root)" : path;

			if (ImGui::MenuItem(label.c_str()))
				m_Pending = PendingAction{PendingAction::Kind::MoveProject, project.Path, path, {}};
		}
	}

	bool SolutionTreeView::RenderFolderRenameEditor(const Folder& folder,
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

		// Scoped to this field: an unscoped key poll would also catch an Escape aimed at a
		// modal elsewhere.
		const bool cancelled = ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape);
		const bool deactivated = !cancelled && ImGui::IsItemDeactivated();

		if (cancelled)
		{
			m_RenamingFolder.clear();
		}
		else if (committed || deactivated)
		{
			const std::string typed = m_RenameBuffer.data();

			if (!typed.empty() && typed != folder.Name)
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
				APP_ERROR("{}", applied.GetError().Message);

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

		// Picked here rather than from the menu that recorded the action: a system dialog
		// opened from inside the tree walk would be modal over a window whose draw is not
		// finished.
		if (action.kind == PendingAction::Kind::AddProject)
		{
			const std::optional<std::filesystem::path> chosen = PickProjectFile();

			if (!chosen.has_value())
				return;

			action.project = *chosen;
		}

		// No reload here: writing the file wakes the directory watcher, and the reload
		// arrives through that path.
		if (const auto applied = Apply(action); !applied)
			APP_ERROR("{}", applied.GetError().Message);
	}

	Core::Result<void> SolutionTreeView::Apply(const PendingAction& action)
	{
		if (!m_Workspace.LoadedSolution())
			return Core::Error{Core::ErrorCode::NotFound, "no solution is open"};

		auto editor = SolutionEditor::Open(m_Workspace.LoadedSolution()->Path);

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