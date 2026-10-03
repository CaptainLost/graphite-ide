#include "Panels/SolutionTreeView.h"

#include "Core/Base/Log.h"
#include "Core/Fs/FileSystem.h"
#include "Core/ImGui/ContextMenu.h"
#include "Core/ImGui/EmptyState.h"
#include "Core/ImGui/Icons.h"
#include "Core/ImGui/UiScale.h"
#include "Core/Platform/FileDialog.h"
#include "Core/Text/DocumentSet.h"
#include "Sln/SolutionEditor.h"

#include <IconsMaterialDesignIcons.h>

#include <algorithm>
#include <filesystem>
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

		Core::Result<std::filesystem::path> PickProjectFile()
		{
			const std::vector<Core::FileDialog::Filter> filters = {
				{"Visual Studio projects", "*.vcxproj"}, {"All files", "*.*"}};

			return Core::FileDialog::PickFile("Add Existing Project", filters);
		}
	}

	SolutionTreeView::SolutionTreeView(Workspace& workspace, const Core::DocumentSet& documents)
		: m_Workspace(workspace), m_Documents(documents)
	{
	}

	void SolutionTreeView::Render()
	{
		if (!m_Workspace.IsOpen() || !m_Workspace.LoadedSolution())
		{
			RenderEmptyState();
			return;
		}

		FollowActiveDocument();
		RenderFilter();
		ImGui::Separator();

		const Solution& solution = *m_Workspace.LoadedSolution();

		if (ImGui::BeginChild("##solutionBody", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
							  ImGuiWindowFlags_HorizontalScrollbar))
		{
			const std::string label =
				solution.Name + " (" + std::to_string(solution.ProjectCount) + " projects)";

			if (OpensToward(solution.Root))
			{
				ImGui::SetNextItemOpen(true);
			}

			const bool rootOpen = ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

			RenderContextMenu(solution.Root, "");

			if (rootOpen)
			{
				RenderFolder(solution.Root, "");
				ImGui::TreePop();
			}
		}

		ImGui::EndChild();
		m_RevealPath.clear();

		// Outside the walk: the executor edits the solution the walk holds references into,
		// and the modal must not be nested inside the child window.
		ExecutePendingAction();
		RenderRemoveConfirmation();
	}

	void SolutionTreeView::FollowActiveDocument()
	{
		// Every change of the active document, however it came about: a go-to-definition
		// into another project, a tab switch, a file opened from search.
		if (!m_FollowsActiveFile)
		{
			// Forgotten, so turning the setting on reveals the document active at that moment.
			m_FollowedPath.clear();
			return;
		}

		const auto active = m_Documents.GetActive();
		const std::filesystem::path path =
			active.has_value() ? m_Documents.At(*active).Path : std::filesystem::path{};

		if (path == m_FollowedPath)
		{
			return;
		}

		m_FollowedPath = path;
		m_RevealPath = path;
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
			{
				continue;
			}

			const std::string label = std::string{ICON_MDI_FOLDER} + "  " + child.Name;

			if (OpensToward(child))
			{
				ImGui::SetNextItemOpen(true);
			}

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
		{
			RenderProject(project, path);
		}
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
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		}

		if (OpensToward(project))
		{
			ImGui::SetNextItemOpen(true);
		}

		const bool open = ImGui::TreeNodeEx(label.c_str(), flags);

		if (!project.Exists)
		{
			ImGui::PopStyleColor();
		}

		RenderContextMenu(project, folder);

		if (!open)
		{
			return;
		}

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
		{
			return;
		}

		for (const std::string& reference : references)
		{
			ImGui::BulletText("%s", reference.c_str());
		}

		ImGui::TreePop();
	}

	void SolutionTreeView::RenderContentFolder(const ContentFolder& folder)
	{
		for (const ContentFolder& child : folder.Folders)
		{
			const std::string label = std::string{ICON_MDI_FOLDER} + "  " + child.Name;

			if (OpensToward(child))
			{
				ImGui::SetNextItemOpen(true);
			}

			if (ImGui::TreeNodeEx(label.c_str()))
			{
				RenderContentFolder(child);
				ImGui::TreePop();
			}
		}

		for (const ContentFile& file : folder.Files)
		{
			if (m_Filter.PassFilter(file.Name.c_str()))
			{
				RenderContentFile(file);
			}
		}
	}

	void SolutionTreeView::RenderContentFile(const ContentFile& file)
	{
		// The active document as well as the clicked row: a document opened some other way,
		// by go-to-definition or from a tab, is what the reveal just opened the tree to show.
		const auto active = m_Documents.GetActive();
		const bool selected = m_Workspace.GetSelectedFile() == file.Path ||
							  (active.has_value() && m_Documents.At(*active).Path == file.Path);

		const std::string label =
			std::string{Core::IconForExtension(Core::ToUtf8(file.Path.extension()))} + "  " +
			file.Name;

		// Keyed by the full path: ImGui gives same-labelled siblings the same id, and two
		// projects can hold files of the same name.
		ImGui::PushID(Core::ToUtf8(file.Path).c_str());

		if (ImGui::Selectable(label.c_str(), selected))
		{
			m_Workspace.SelectFile(file.Path);
		}

		if (!m_RevealPath.empty() && m_RevealPath == file.Path && !ImGui::IsItemVisible())
		{
			ImGui::SetScrollHereY(0.5f);
		}

		ImGui::PopID();
	}

	void SolutionTreeView::RenderEmptyState()
	{
		if (!m_Workspace.IsOpen())
		{
			Core::EmptyState("Open a folder to see its Visual Studio solution.");
			return;
		}

		if (m_Workspace.GetSolutionCandidates().empty())
		{
			Core::EmptyState("No Visual Studio solution in this folder.");
			return;
		}

		Core::EmptyState("This Visual Studio solution could not be read. See the console.");
	}

	void SolutionTreeView::RenderContextMenu(const Folder& folder,
											 const std::string& path)
	{
		const std::string popupId = "##ctxSlnFolder" + path;

		if (!Core::BeginRightClickPopup(popupId.c_str()))
		{
			return;
		}

		const bool isRoot = path.empty();

		if (ImGui::MenuItem("Add Existing Project...", nullptr, false, !m_Workspace.IsDialogOpen()))
		{
			m_Pending = PendingAction{PendingAction::Kind::AddProject, {}, path, {}};
		}

		if (ImGui::MenuItem("New Folder"))
		{
			m_Pending =
				PendingAction{PendingAction::Kind::AddFolder, {}, ChildPath(path, "New Folder"), {}};
		}

		if (!isRoot)
		{
			ImGui::Separator();

			if (ImGui::MenuItem("Rename"))
			{
				StartRenamingFolder(folder, path);
			}

			if (ImGui::MenuItem("Remove"))
			{
				m_Pending = PendingAction{PendingAction::Kind::RemoveFolder, {}, path, {}};
			}
		}

		ImGui::EndPopup();
	}

	void SolutionTreeView::StartRenamingFolder(const Folder& folder, const std::string& path)
	{
		m_RenamingFolder = path;
		m_FocusRenameField = true;

		std::fill(m_RenameBuffer.begin(), m_RenameBuffer.end(), '\0');

		if (folder.Name.size() < m_RenameBuffer.size())
		{
			std::copy_n(folder.Name.begin(), folder.Name.size(), m_RenameBuffer.begin());
		}
	}

	void SolutionTreeView::RenderContextMenu(const Project& project,
											 const std::string& folder)
	{
		const std::string popupId = "##ctxSlnProject" + Core::ToUtf8(project.Path);

		if (!Core::BeginRightClickPopup(popupId.c_str()))
		{
			return;
		}

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
		{
			return;
		}

		std::vector<std::string> folders{""};
		CollectFolderPaths(m_Workspace.LoadedSolution()->Root, "", folders);

		for (const std::string& path : folders)
		{
			if (path == currentFolder)
			{
				continue;
			}

			const std::string label = path.empty() ? "(Solution Root)" : path;

			if (ImGui::MenuItem(label.c_str()))
			{
				m_Pending = PendingAction{PendingAction::Kind::MoveProject, project.Path, path, {}};
			}
		}
	}

	bool SolutionTreeView::RenderFolderRenameEditor(const Folder& folder,
													const std::string& path)
	{
		if (m_RenamingFolder != path)
		{
			return false;
		}

		if (m_FocusRenameField)
		{
			ImGui::SetKeyboardFocusHere();
			m_FocusRenameField = false;
		}

		ImGui::SetNextItemWidth(Core::Scaled(200.0f));

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
			{
				m_Pending = PendingAction{PendingAction::Kind::RenameFolder, {}, path, typed};
			}

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
			{
				APP_ERROR("{}", applied.GetError().Message);
			}

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
		if (m_Pending.Action == PendingAction::Kind::None)
		{
			return;
		}

		PendingAction action = std::move(m_Pending);
		m_Pending = PendingAction{};

		// The dialog is started here rather than from the menu that recorded the action, and
		// answers on a later frame: it runs on a thread of its own. By then the solution may
		// have closed, which Apply already reports.
		if (action.Action == PendingAction::Kind::AddProject)
		{
			m_Workspace.Dialog().Start(PickProjectFile,
									   [this, action = std::move(action)](Core::Result<std::filesystem::path> picked) mutable {
										   if (!picked)
										   {
											   APP_ERROR("Cannot open the file picker: {}", picked.GetError().Message);
											   return;
										   }

										   if (picked.GetValue().empty())
										   {
											   return;
										   }

										   action.Project = std::move(picked).GetValue();
										   ApplyAndReport(action);
									   });
			return;
		}

		ApplyAndReport(action);
	}

	void SolutionTreeView::ApplyAndReport(const PendingAction& action)
	{
		// No reload here: writing the file wakes the directory watcher, and the reload
		// arrives through that path.
		if (const auto applied = Apply(action); !applied)
		{
			APP_ERROR("{}", applied.GetError().Message);
		}
	}

	Core::Result<void> SolutionTreeView::Apply(const PendingAction& action)
	{
		if (!m_Workspace.LoadedSolution())
		{
			return Core::Error{Core::ErrorCode::NotFound, "no solution is open"};
		}

		auto editor = SolutionEditor::Open(m_Workspace.LoadedSolution()->Path);

		if (!editor)
		{
			return editor.GetError();
		}

		Core::Result<void> applied = Core::Result<void>::Ok();

		switch (action.Action)
		{
		case PendingAction::Kind::AddProject:
			applied = editor.GetValue()->AddProject(action.Project, action.Folder);
			break;

		case PendingAction::Kind::RemoveProject:
			applied = editor.GetValue()->RemoveProject(action.Project);
			break;

		case PendingAction::Kind::AddFolder:
			applied = editor.GetValue()->AddFolder(action.Folder);
			break;

		case PendingAction::Kind::RemoveFolder:
			applied = editor.GetValue()->RemoveFolder(action.Folder);
			break;

		case PendingAction::Kind::RenameFolder:
			applied = editor.GetValue()->RenameFolder(action.Folder, action.Name);
			break;

		case PendingAction::Kind::MoveProject:
			applied = editor.GetValue()->MoveProject(action.Project, action.Folder);
			break;

		case PendingAction::Kind::None:
			return Core::Result<void>::Ok();
		}

		if (!applied)
		{
			return applied;
		}

		return editor.GetValue()->Save();
	}
}