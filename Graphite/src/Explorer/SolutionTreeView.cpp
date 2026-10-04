#include "Explorer/SolutionTreeView.h"

#include "Core/Base/Log.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/ContextMenu.h"
#include "Core/ImGui/EmptyState.h"
#include "Core/ImGui/Icons.h"
#include "Core/ImGui/UiScale.h"
#include "Core/Platform/FileDialog.h"
#include "Core/Text/DocumentSet.h"
#include "Explorer/FileRow.h"
#include "Solution/SolutionEditor.h"
#include "UI/OpenGesture.h"

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

		// The folders a project can be moved to: the solution root and every folder but the one it
		// is in.
		std::vector<std::string> MoveTargets(const Solution& solution, const std::string& currentFolder)
		{
			std::vector<std::string> folders{""};
			CollectFolderPaths(solution.Root, "", folders);

			std::erase(folders, currentFolder);
			return folders;
		}

		constexpr const char* kNotEditableTip =
			"Editing .sln files is not supported yet; convert the solution to .slnx";

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
		// Before the empty state: a reload that failed, or a folder that closed, ends the
		// wait for the folder just added.
		m_RenameWhenListed.BeginFrame(m_Workspace.IsOpen() && CanEditSolution(),
									  m_Workspace.GetSolutionLoad());

		if (!m_Workspace.IsOpen() || !m_Workspace.GetLoadedSolution())
		{
			RenderEmptyState();
			return;
		}

		FollowActiveDocument();
		RenderFilter();
		ImGui::Separator();

		const Solution& solution = *m_Workspace.GetLoadedSolution();

		if (ImGui::BeginChild("##solutionBody", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
							  ImGuiWindowFlags_HorizontalScrollbar))
		{
			const std::string label =
				solution.Name + " (" + std::to_string(solution.ProjectCount) + " projects)";

			if (OpensToward(solution.Root) || m_RenameWhenListed.IsLooking())
			{
				ImGui::SetNextItemOpen(true);
			}

			const bool rootOpen = ImGui::TreeNodeEx(
				label.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);

			RenderContextMenu(solution.Root, "");

			if (rootOpen)
			{
				RenderFolder(solution.Root, "");
				ImGui::TreePop();
			}
		}

		ImGui::EndChild();
		m_RevealPath.clear();
		m_RenameWhenListed.EndFrame();

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

			// A folder just added, now that the reloaded solution lists it.
			if (m_RenameWhenListed.Take(childPath))
			{
				StartRenamingFolder(child, childPath);
			}

			if (RenderFolderRenameEditor(child, childPath))
			{
				continue;
			}

			const std::string label = std::string{ICON_MDI_FOLDER} + "  " + child.Name;

			if (OpensToward(child) || m_RenameWhenListed.LeadsTo(childPath))
			{
				ImGui::SetNextItemOpen(true);
			}

			const bool open = ImGui::TreeNodeEx(
				label.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);

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
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;

		if (!project.ContentRead)
		{
			flags |= ImGuiTreeNodeFlags_Leaf;
		}

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
		if (references.empty() || !ImGui::TreeNodeEx("References", ImGuiTreeNodeFlags_SpanAvailWidth))
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

			if (ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth))
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

		std::string label =
			std::string{Core::IconForExtension(Core::ToUtf8(file.Path.extension()))} + "  " +
			file.Name;

		if (!file.Exists)
		{
			label += " (missing)";
		}

		const FileRowLayout layout = LayoutFileRow(label);

		// Keyed by the full path: ImGui gives same-labelled siblings the same id, and two
		// projects can hold files of the same name.
		const std::string id = "##" + Core::ToUtf8(file.Path);

		// A file the project lists but the disk lacks has nothing to open.
		const ImGuiSelectableFlags flags =
			ImGuiSelectableFlags_AllowDoubleClick | (file.Exists ? 0 : ImGuiSelectableFlags_Disabled);

		const bool clicked = ImGui::Selectable(id.c_str(), selected, flags, ImVec2(layout.Width, 0.0f));

		DrawFileRowLabel(layout,
						 FileRowLook{.Ink = ImGui::GetColorU32(file.Exists ? ImGuiCol_Text : ImGuiCol_TextDisabled)},
						 label);

		if (!file.Exists && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::SetTooltip("File not found on disk");
		}

		if (!m_RevealPath.empty() && m_RevealPath == file.Path && !ImGui::IsItemVisible())
		{
			ImGui::SetScrollHereY(0.5f);
		}

		// As in the file tree: a click selects and opens a preview, a double click keeps it.
		if (clicked)
		{
			m_Workspace.SelectFile(file.Path);
			m_Workspace.RequestOpenFile(file.Path, OpensPermanently());
		}
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

		const bool editable = CanEditSolution();

		if (ImGui::MenuItem("Add Existing Project...", nullptr, false, editable && !m_Workspace.IsDialogOpen()))
		{
			m_Pending = PendingAction{PendingAction::Kind::AddProject, {}, path, {}};
		}

		ExplainNotEditable(editable);

		if (ImGui::MenuItem("New Folder", nullptr, false, editable))
		{
			// Named here, where the siblings are at hand: a second one under the same name
			// would be refused.
			m_Pending = PendingAction{PendingAction::Kind::AddFolder, {},
									  ChildPath(path, UniqueFolderName(folder, "New Folder")), {}};
		}

		ExplainNotEditable(editable);

		if (!isRoot)
		{
			ImGui::Separator();

			if (ImGui::MenuItem("Rename", nullptr, false, editable))
			{
				StartRenamingFolder(folder, path);
			}

			ExplainNotEditable(editable);

			if (ImGui::MenuItem("Remove", nullptr, false, editable))
			{
				m_Pending = PendingAction{PendingAction::Kind::RemoveFolder, {}, path, {}};
			}

			ExplainNotEditable(editable);
		}

		ImGui::EndPopup();
	}

	bool SolutionTreeView::CanEditSolution() const
	{
		return m_Workspace.GetLoadedSolution() && SolutionEditor::CanEdit(m_Workspace.GetLoadedSolution()->Path);
	}

	void SolutionTreeView::ExplainNotEditable(bool editable)
	{
		// The item above is the one disabled, so its hover is the one to answer.
		if (!editable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::SetTooltip("%s", kNotEditableTip);
		}
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

		const bool editable = CanEditSolution();

		if (ImGui::MenuItem("Remove from Solution", nullptr, false, editable))
		{
			m_RemoveCandidate = project.Path;
			m_RemoveCandidateName = project.DisplayName;
			m_OpenRemoveConfirmation = true;
		}

		ExplainNotEditable(editable);

		// With nowhere else to go the submenu would open empty.
		const bool hasTarget =
			m_Workspace.GetLoadedSolution() && !MoveTargets(*m_Workspace.GetLoadedSolution(), folder).empty();

		if (ImGui::BeginMenu("Move to Folder", editable && hasTarget))
		{
			RenderMoveToFolderMenu(project, folder);
			ImGui::EndMenu();
		}

		ExplainNotEditable(editable);

		ImGui::EndPopup();
	}

	void SolutionTreeView::RenderMoveToFolderMenu(const Project& project,
												  const std::string& currentFolder)
	{
		if (!m_Workspace.GetLoadedSolution())
		{
			return;
		}

		for (const std::string& path : MoveTargets(*m_Workspace.GetLoadedSolution(), currentFolder))
		{
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
			m_Workspace.GetDialog().Start(PickProjectFile,
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
			return;
		}

		// Named by the user next, in place, as soon as the reloaded solution lists it.
		if (action.Action == PendingAction::Kind::AddFolder)
		{
			m_RenameWhenListed.Request(action.Folder, m_Workspace.GetSolutionLoad());
		}
	}

	Core::Result<void> SolutionTreeView::Apply(const PendingAction& action)
	{
		if (!m_Workspace.GetLoadedSolution())
		{
			return Core::Error{Core::ErrorCode::NotFound, "no solution is open"};
		}

		auto editor = SolutionEditor::Open(m_Workspace.GetLoadedSolution()->Path);

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