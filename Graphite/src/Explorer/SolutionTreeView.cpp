#include "Explorer/SolutionTreeView.h"

#include "Core/Base/Log.h"
#include "Core/Events/EventBus.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/ContextMenu.h"
#include "Core/ImGui/EmptyState.h"
#include "Core/ImGui/Icons.h"
#include "Core/ImGui/UiScale.h"
#include "Core/Platform/FileDialog.h"
#include "Core/Text/DocumentSet.h"
#include "Explorer/FileRow.h"
#include "Git/GitDecorationView.h"
#include "Solution/SolutionEditor.h"
#include "UI/OpenGesture.h"
#include "UI/ToastEvent.h"

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

		void CollectFilePaths(const ContentFolder& folder, std::vector<std::filesystem::path>& out)
		{
			for (const ContentFile& file : folder.Files)
			{
				// A file the disk lacks has no status to show.
				if (file.Exists)
				{
					out.push_back(file.Path);
				}
			}

			for (const ContentFolder& child : folder.Folders)
			{
				CollectFilePaths(child, out);
			}
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

	SolutionTreeView::SolutionTreeView(Workspace& workspace, const Core::DocumentSet& documents,
									   Core::EventBus& events)
		: m_Workspace(workspace), m_Documents(documents), m_Events(events)
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

		// Held back while filtering, so a document opened from the results is revealed once the
		// filter is cleared: revealed in rows that are not drawn, it would be lost.
		if (!m_Filter.IsActive())
		{
			FollowActiveDocument();
		}

		RenderFilter();
		ImGui::Separator();

		const Solution& solution = *m_Workspace.GetLoadedSolution();

		ForgetStaleGroupDecorations();
		ForgetStaleFilterMatches();

		if (ImGui::BeginChild("##solutionBody", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
							  ImGuiWindowFlags_HorizontalScrollbar))
		{
			constexpr const char* kFilteredTreeScope = "##filteredTree";
			const bool filtering = m_Filter.IsActive();

			// ImGui keeps a row's open state by its ID: without a scope of their own, the rows
			// the filter opens would stay open once it is cleared.
			if (filtering)
			{
				ImGui::PushID(kFilteredTreeScope);
			}

			const std::string label =
				solution.Name + " (" + std::to_string(solution.ProjectCount) + " projects)";

			if (OpensToward(solution.Root) || m_RenameWhenListed.IsLooking() || m_ReopenFilteredRows)
			{
				ImGui::SetNextItemOpen(true);
			}

			const bool rootOpen = ImGui::TreeNodeEx(
				label.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);

			DrawGroupMarker(solution.Root);
			RenderContextMenu(solution.Root, "");

			if (rootOpen)
			{
				RenderFolder(solution.Root, "");
				ImGui::TreePop();
			}

			if (filtering)
			{
				ImGui::PopID();
			}

			if (filtering && !HoldsMatch(solution.Root))
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
				ImGui::TextWrapped("No files match \"%s\"", m_Filter.InputBuf);
				ImGui::PopStyleColor();
			}
		}

		ImGui::EndChild();
		m_ReopenFilteredRows = false;
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

		const auto active = m_Documents.GetActiveIndex();
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
			if (IsShown(child))
			{
				RenderChildFolder(child, ChildPath(path, child.Name));
			}
		}

		for (const Project& project : folder.Projects)
		{
			if (IsShown(project))
			{
				RenderProject(project, path);
			}
		}
	}

	void SolutionTreeView::RenderChildFolder(const Folder& folder, const std::string& path)
	{
		// A folder just added, now that the reloaded solution lists it.
		if (m_RenameWhenListed.Take(path))
		{
			StartRenamingFolder(folder, path);
		}

		if (RenderFolderRenameEditor(folder, path))
		{
			return;
		}

		const std::string label = std::string{ICON_MDI_FOLDER} + "  " + folder.Name;

		if (m_ReopenFilteredRows)
		{
			ImGui::SetNextItemOpen(HoldsMatch(folder));
		}

		if (OpensToward(folder) || m_RenameWhenListed.LeadsTo(path))
		{
			ImGui::SetNextItemOpen(true);
		}

		const bool open = ImGui::TreeNodeEx(
			label.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);

		DrawGroupMarker(folder);
		RenderContextMenu(folder, path);

		if (open)
		{
			RenderFolder(folder, path);
			ImGui::TreePop();
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

		if (m_ReopenFilteredRows)
		{
			ImGui::SetNextItemOpen(HoldsMatch(project));
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

		DrawGroupMarker(project);
		RenderContextMenu(project, folder);

		if (!open)
		{
			return;
		}

		if (project.ContentRead)
		{
			if (!m_Filter.IsActive())
			{
				RenderReferences(project.Content.References);
			}

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
			if (IsShown(child))
			{
				const std::string label = std::string{ICON_MDI_FOLDER} + "  " + child.Name;

				if (m_ReopenFilteredRows)
				{
					ImGui::SetNextItemOpen(HoldsMatch(child));
				}

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
		// Not the active document, which stays active behind Settings or the Git view.
		const bool isShownInEditor = !m_ShownFile.empty() && m_ShownFile == file.Path;

		std::string label =
			std::string{Core::IconForExtension(Core::ToUtf8(file.Path.extension()))} + "  " +
			file.Name;

		if (!file.Exists)
		{
			label += " (missing)";
		}

		const FileRowLayout layout = LayoutFileRow(label, m_GitDecorations.Index != nullptr);

		// Keyed by the full path: ImGui gives same-labelled siblings the same id, and two
		// projects can hold files of the same name.
		const std::string id = "##" + Core::ToUtf8(file.Path);

		// A file the project lists but the disk lacks has nothing to open.
		const ImGuiSelectableFlags flags =
			ImGuiSelectableFlags_AllowDoubleClick | (file.Exists ? 0 : ImGuiSelectableFlags_Disabled);

		const bool clicked = ImGui::Selectable(id.c_str(), isShownInEditor, flags, ImVec2(layout.Width, 0.0f));

		// Only for a row on screen: the lookup builds a path key. A file the disk lacks keeps its
		// missing look and shows no status.
		if (ImGui::IsItemVisible())
		{
			FileRowLook look{.Ink = ImGui::GetColorU32(file.Exists ? ImGuiCol_Text : ImGuiCol_TextDisabled)};

			if (file.Exists)
			{
				look.IsOpenInTab = m_Documents.Find(file.Path).has_value();
				look.Status = FileRowStatusOf(m_GitDecorations, file.Path);

				if (look.Status.Decoration != GitDecoration::None)
				{
					look.Ink = look.Status.Ink;
				}
			}

			DrawFileRowLabel(layout, look, label);
		}

		if (!file.Exists && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::SetTooltip("File not found on disk");
		}

		if (!m_RevealPath.empty() && m_RevealPath == file.Path && !ImGui::IsItemVisible())
		{
			ImGui::SetScrollHereY(0.5f);
		}

		if (clicked)
		{
			m_Workspace.RequestOpenFile(file.Path, OpensPermanently());
		}
	}

	void SolutionTreeView::ForgetStaleFilterMatches()
	{
		const std::string filterText = m_Filter.InputBuf;
		const std::uint64_t load = m_Workspace.GetSolutionLoad();
		const bool filterTextChanged = filterText != m_FilterMatchText;

		if (!filterTextChanged && load == m_FilterMatchLoad)
		{
			return;
		}

		m_HoldsMatch.clear();
		m_FilterMatchText = filterText;
		m_FilterMatchLoad = load;

		if (filterTextChanged && m_Filter.IsActive())
		{
			m_ReopenFilteredRows = true;
		}
	}

	bool SolutionTreeView::IsShown(const Folder& folder)
	{
		return !m_Filter.IsActive() || m_Filter.PassFilter(folder.Name.c_str()) || HoldsMatch(folder);
	}

	bool SolutionTreeView::IsShown(const Project& project)
	{
		return !m_Filter.IsActive() || m_Filter.PassFilter(project.DisplayName.c_str()) || HoldsMatch(project);
	}

	bool SolutionTreeView::IsShown(const ContentFolder& folder)
	{
		return !m_Filter.IsActive() || m_Filter.PassFilter(folder.Name.c_str()) || HoldsMatch(folder);
	}

	bool SolutionTreeView::HoldsMatch(const Folder& folder)
	{
		if (m_HoldsMatch.contains(&folder))
		{
			return m_HoldsMatch.at(&folder);
		}

		const bool holdsMatch = AnyFolderShown(folder) || AnyProjectShown(folder);
		m_HoldsMatch.emplace(&folder, holdsMatch);

		return holdsMatch;
	}

	bool SolutionTreeView::HoldsMatch(const Project& project)
	{
		return project.ContentRead && HoldsMatch(project.Content.Root);
	}

	bool SolutionTreeView::HoldsMatch(const ContentFolder& folder)
	{
		if (m_HoldsMatch.contains(&folder))
		{
			return m_HoldsMatch.at(&folder);
		}

		const bool holdsMatch = AnyFilePasses(folder) || AnyFolderShown(folder);
		m_HoldsMatch.emplace(&folder, holdsMatch);

		return holdsMatch;
	}

	bool SolutionTreeView::AnyFilePasses(const ContentFolder& folder) const
	{
		for (const ContentFile& file : folder.Files)
		{
			if (m_Filter.PassFilter(file.Name.c_str()))
			{
				return true;
			}
		}

		return false;
	}

	bool SolutionTreeView::AnyFolderShown(const ContentFolder& folder)
	{
		for (const ContentFolder& child : folder.Folders)
		{
			if (IsShown(child))
			{
				return true;
			}
		}

		return false;
	}

	bool SolutionTreeView::AnyFolderShown(const Folder& folder)
	{
		for (const Folder& child : folder.Folders)
		{
			if (IsShown(child))
			{
				return true;
			}
		}

		return false;
	}

	bool SolutionTreeView::AnyProjectShown(const Folder& folder)
	{
		for (const Project& project : folder.Projects)
		{
			if (IsShown(project))
			{
				return true;
			}
		}

		return false;
	}

	void SolutionTreeView::ForgetStaleGroupDecorations()
	{
		const std::uint64_t load = m_Workspace.GetSolutionLoad();

		if (load == m_GroupLoad && m_GitDecorations.Index == m_GroupIndex &&
			m_GitDecorations.Version == m_GroupVersion)
		{
			return;
		}

		m_GroupDecorations.clear();
		m_GroupLoad = load;
		m_GroupIndex = m_GitDecorations.Index;
		m_GroupVersion = m_GitDecorations.Version;
	}

	GitDecoration SolutionTreeView::GroupDecorationOf(const Project& project)
	{
		if (!project.Exists || !project.ContentRead || m_GitDecorations.Index == nullptr)
		{
			return GitDecoration::None;
		}

		if (const auto cached = m_GroupDecorations.find(&project); cached != m_GroupDecorations.end())
		{
			return cached->second;
		}

		std::vector<std::filesystem::path> files;
		CollectFilePaths(project.Content.Root, files);

		const GitDecoration decoration = MaxDecorationOf(files, m_GitDecorations);
		m_GroupDecorations.emplace(&project, decoration);

		return decoration;
	}

	GitDecoration SolutionTreeView::GroupDecorationOf(const Folder& folder)
	{
		if (m_GitDecorations.Index == nullptr)
		{
			return GitDecoration::None;
		}

		if (const auto cached = m_GroupDecorations.find(&folder); cached != m_GroupDecorations.end())
		{
			return cached->second;
		}

		GitDecoration decoration = GitDecoration::None;

		for (const Folder& child : folder.Folders)
		{
			decoration = std::max(decoration, GroupDecorationOf(child));
		}

		for (const Project& project : folder.Projects)
		{
			decoration = std::max(decoration, GroupDecorationOf(project));
		}

		m_GroupDecorations.emplace(&folder, decoration);

		return decoration;
	}

	void SolutionTreeView::DrawGroupMarker(const Project& project)
	{
		if (ImGui::IsItemVisible())
		{
			DrawGroupMarker(GroupDecorationOf(project));
		}
	}

	void SolutionTreeView::DrawGroupMarker(const Folder& folder)
	{
		if (ImGui::IsItemVisible())
		{
			DrawGroupMarker(GroupDecorationOf(folder));
		}
	}

	void SolutionTreeView::DrawGroupMarker(GitDecoration decoration)
	{
		if (decoration == GitDecoration::None)
		{
			return;
		}

		RowStatus status;
		status.Decoration = decoration;
		status.Dot = true;
		status.Ink = m_GitDecorations.ColourFor(decoration);

		const float textTop =
			ImGui::GetItemRectMin().y + (ImGui::GetItemRectSize().y - ImGui::GetFontSize()) * 0.5f;
		DrawRowMarker(status, textTop);
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
				m_Events.Publish(FolderToast(Core::ToastKind::Error, "Solution edit failed", applied.GetError().Message,
												 m_Workspace.GetRoot()));
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
												  m_Events.Publish(ApplicationToast(Core::ToastKind::Error, "Cannot open file picker",
																									picked.GetError().Message));
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
			m_Events.Publish(FolderToast(Core::ToastKind::Error, "Solution edit failed", applied.GetError().Message,
											 m_Workspace.GetRoot()));
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