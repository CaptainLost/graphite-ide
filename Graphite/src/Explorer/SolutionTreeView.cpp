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
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {
		const std::string kSolutionRootPath;
		constexpr const char* kNoShortcut = nullptr;
		constexpr bool kNotSelected = false;
		constexpr bool kOpenTheRow = true;
		constexpr float kHalf = 0.5f;
		constexpr float kRenameFieldWidth = 200.0f;
		constexpr float kConfirmButtonWidth = 90.0f;

		std::string ChildPath(const std::string& path, const std::string& name)
		{
			return path.empty() ? name : path + "/" + name;
		}

		void CollectFolderPaths(const Folder& folder, const std::string& path,
								std::vector<std::string>& collectedPaths)
		{
			for (const Folder& child : folder.Folders)
			{
				const std::string childPath = ChildPath(path, child.Name);
				collectedPaths.push_back(childPath);
				CollectFolderPaths(child, childPath, collectedPaths);
			}
		}

		std::vector<std::string> MoveTargets(const Solution& solution, const std::string& currentFolder)
		{
			std::vector<std::string> folders{kSolutionRootPath};
			CollectFolderPaths(solution.Root, kSolutionRootPath, folders);

			std::erase(folders, currentFolder);
			return folders;
		}

		void CollectFilePaths(const ContentFolder& folder, std::vector<std::filesystem::path>& collectedPaths)
		{
			for (const ContentFile& file : folder.Files)
			{
				if (file.Exists)
				{
					collectedPaths.push_back(file.Path);
				}
			}

			for (const ContentFolder& child : folder.Folders)
			{
				CollectFilePaths(child, collectedPaths);
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
		m_RenameWhenListed.BeginFrame(m_Workspace.IsOpen() && CanEditSolution(),
									  m_Workspace.GetSolutionLoad());

		if (!m_Workspace.IsOpen() || !m_Workspace.GetLoadedSolution())
		{
			RenderEmptyState();
			return;
		}

		if (!m_Filter.IsActive())
		{
			FollowActiveDocument();
		}

		RenderFilter();
		ImGui::Separator();

		const Solution& solution = *m_Workspace.GetLoadedSolution();

		ForgetStaleGroupDecorations();
		ForgetStaleFilterMatches();

		if (ImGui::BeginChild("##solutionBody", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None))
		{
			constexpr const char* kFilteredTreeScope = "##filteredTree";
			const bool filtering = m_Filter.IsActive();

			// WARNING: ImGui keeps a row's open state by its ID. Without a scope of their own, the rows the
			// filter opens would stay open once it is cleared.
			if (filtering)
			{
				ImGui::PushID(kFilteredTreeScope);
			}

			const std::string label =
				solution.Name + " (" + std::to_string(solution.ProjectCount) + " projects)";

			if (OpensToward(solution.Root) || m_RenameWhenListed.IsLooking() || m_ReopenFilteredRows)
			{
				ImGui::SetNextItemOpen(kOpenTheRow);
			}

			const bool rootOpen = ImGui::TreeNodeEx(
				label.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth, "%s", "");
			DrawTreeRowLabel(label, ImGui::IsItemVisible() && GroupDecorationOf(solution.Root) != GitDecoration::None);

			DrawGroupMarker(solution.Root);
			RenderContextMenu(solution.Root, kSolutionRootPath);

			if (rootOpen)
			{
				RenderFolder(solution.Root, kSolutionRootPath);
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

		// WARNING: Run outside the walk. The executor edits the solution the walk holds references into, and the
		// modal must not be nested inside the child window.
		ExecutePendingAction();
		RenderRemoveConfirmation();
	}

	void SolutionTreeView::FollowActiveDocument()
	{
		if (!m_FollowsActiveFile)
		{
			m_FollowedPath.clear();
			return;
		}

		const std::optional<std::size_t> activeIndex = m_Documents.GetActiveIndex();
		const std::filesystem::path activePath =
			activeIndex.has_value() ? m_Documents.At(*activeIndex).Path : std::filesystem::path{};

		if (activePath == m_FollowedPath)
		{
			return;
		}

		m_FollowedPath = activePath;
		m_RevealPath = activePath;
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
			ImGui::SetNextItemOpen(kOpenTheRow);
		}

		const bool isOpen = ImGui::TreeNodeEx(
			label.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth, "%s", "");
		DrawTreeRowLabel(label, ImGui::IsItemVisible() && GroupDecorationOf(folder) != GitDecoration::None);

		DrawGroupMarker(folder);
		RenderContextMenu(folder, path);

		if (isOpen)
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
		const std::string shownName = project.Exists ? project.DisplayName : project.DisplayName + "  (missing)";
		const std::string label = icon + "  " + shownName;

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
			ImGui::SetNextItemOpen(kOpenTheRow);
		}

		const bool isOpen = ImGui::TreeNodeEx(label.c_str(), flags, "%s", "");
		DrawTreeRowLabel(label, ImGui::IsItemVisible() && GroupDecorationOf(project) != GitDecoration::None);

		if (!project.Exists)
		{
			ImGui::PopStyleColor();
		}

		DrawGroupMarker(project);
		RenderContextMenu(project, folder);

		if (!isOpen)
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
					ImGui::SetNextItemOpen(kOpenTheRow);
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
		const bool isShownInEditor = !m_ShownFile.empty() && m_ShownFile == file.Path;

		std::string label =
			std::string{Core::IconForExtension(Core::ToUtf8(file.Path.extension()))} + "  " +
			file.Name;

		if (!file.Exists)
		{
			label += " (missing)";
		}

		const FileRowLayout layout = LayoutFileRow();

		// WARNING: ImGui gives same-labelled siblings the same id, and two projects can hold files of the
		// same name, so the id is the full path.
		const std::string id = "##" + Core::ToUtf8(file.Path);

		const ImGuiSelectableFlags flags =
			ImGuiSelectableFlags_AllowDoubleClick | (file.Exists ? 0 : ImGuiSelectableFlags_Disabled);

		const bool clicked = ImGui::Selectable(id.c_str(), isShownInEditor, flags, ImVec2(layout.Width, 0.0f));

		if (ImGui::IsItemVisible())
		{
			FileRowLook rowLook{.Ink = ImGui::GetColorU32(file.Exists ? ImGuiCol_Text : ImGuiCol_TextDisabled)};

			if (file.Exists)
			{
				rowLook.Tab = OpenTabOf(m_Documents, file.Path);
				rowLook.Status = FileRowStatusOf(m_GitDecorations, file.Path);

				if (rowLook.Status.Decoration != GitDecoration::None)
				{
					rowLook.Ink = rowLook.Status.Ink;
				}
			}

			DrawFileRowLabel(layout, rowLook, label);
		}

		if (!file.Exists && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::SetTooltip("File not found on disk");
		}

		if (!m_RevealPath.empty() && m_RevealPath == file.Path && !ImGui::IsItemVisible())
		{
			ImGui::SetScrollHereY(kHalf);
		}

		if (clicked)
		{
			m_Workspace.RequestOpenFile(file.Path, OpensPermanently());
		}
	}

	void SolutionTreeView::ForgetStaleFilterMatches()
	{
		const std::string filterText = m_Filter.InputBuf;
		const std::uint64_t solutionLoad = m_Workspace.GetSolutionLoad();
		const bool filterTextChanged = filterText != m_FilterMatchText;

		if (!filterTextChanged && solutionLoad == m_FilterMatchLoad)
		{
			return;
		}

		m_HoldsMatch.clear();
		m_FilterMatchText = filterText;
		m_FilterMatchLoad = solutionLoad;

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
		const std::uint64_t solutionLoad = m_Workspace.GetSolutionLoad();

		if (solutionLoad == m_GroupLoad && m_GitDecorations.Index == m_GroupIndex &&
			m_GitDecorations.Version == m_GroupVersion)
		{
			return;
		}

		m_GroupDecorations.clear();
		m_GroupLoad = solutionLoad;
		m_GroupIndex = m_GitDecorations.Index;
		m_GroupVersion = m_GitDecorations.Version;
	}

	GitDecoration SolutionTreeView::GroupDecorationOf(const Project& project)
	{
		if (!project.Exists || !project.ContentRead || m_GitDecorations.Index == nullptr)
		{
			return GitDecoration::None;
		}

		if (const std::unordered_map<const void*, GitDecoration>::iterator cached = m_GroupDecorations.find(&project);
			cached != m_GroupDecorations.end())
		{
			return cached->second;
		}

		std::vector<std::filesystem::path> filePaths;
		CollectFilePaths(project.Content.Root, filePaths);

		const GitDecoration decoration = MaxDecorationOf(filePaths, m_GitDecorations);
		m_GroupDecorations.emplace(&project, decoration);

		return decoration;
	}

	GitDecoration SolutionTreeView::GroupDecorationOf(const Folder& folder)
	{
		if (m_GitDecorations.Index == nullptr)
		{
			return GitDecoration::None;
		}

		if (const std::unordered_map<const void*, GitDecoration>::iterator cached = m_GroupDecorations.find(&folder);
			cached != m_GroupDecorations.end())
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
			ImGui::GetItemRectMin().y + (ImGui::GetItemRectSize().y - ImGui::GetFontSize()) * kHalf;
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

		if (ImGui::MenuItem("Add Existing Project...", kNoShortcut, kNotSelected, editable && !m_Workspace.IsDialogOpen()))
		{
			m_Pending = PendingAction{.Action = PendingAction::Kind::AddProject, .Folder = path};
		}

		ExplainNotEditable(editable);

		if (ImGui::MenuItem("New Folder", kNoShortcut, kNotSelected, editable))
		{
			m_Pending = PendingAction{.Action = PendingAction::Kind::AddFolder,
									  .Folder = ChildPath(path, UniqueFolderName(folder, "New Folder"))};
		}

		ExplainNotEditable(editable);

		if (!isRoot)
		{
			ImGui::Separator();

			if (ImGui::MenuItem("Rename", kNoShortcut, kNotSelected, editable))
			{
				StartRenamingFolder(folder, path);
			}

			ExplainNotEditable(editable);

			if (ImGui::MenuItem("Remove", kNoShortcut, kNotSelected, editable))
			{
				m_Pending = PendingAction{.Action = PendingAction::Kind::RemoveFolder, .Folder = path};
			}

			ExplainNotEditable(editable);
		}

		ImGui::EndPopup();
	}

	bool SolutionTreeView::OpensToward(const Folder& folder) const
	{
		return !m_RevealPath.empty() && HoldsFile(folder, m_RevealPath);
	}

	bool SolutionTreeView::OpensToward(const Project& project) const
	{
		return !m_RevealPath.empty() && HoldsFile(project, m_RevealPath);
	}

	bool SolutionTreeView::OpensToward(const ContentFolder& folder) const
	{
		return !m_RevealPath.empty() && HoldsFile(folder, m_RevealPath);
	}

	bool SolutionTreeView::CanEditSolution() const
	{
		return m_Workspace.GetLoadedSolution() && SolutionEditor::CanEdit(m_Workspace.GetLoadedSolution()->Path);
	}

	void SolutionTreeView::ExplainNotEditable(bool editable)
	{
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

		if (ImGui::MenuItem("Remove from Solution", kNoShortcut, kNotSelected, editable))
		{
			m_RemoveCandidate = project.Path;
			m_RemoveCandidateName = project.DisplayName;
			m_OpenRemoveConfirmation = true;
		}

		ExplainNotEditable(editable);

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
				m_Pending = PendingAction{.Action = PendingAction::Kind::MoveProject, .Project = project.Path, .Folder = path};
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

		ImGui::SetNextItemWidth(Core::Scaled(kRenameFieldWidth));

		const bool committed =
			ImGui::InputText("##renameSlnFolder", m_RenameBuffer.data(), m_RenameBuffer.size(),
							 ImGuiInputTextFlags_EnterReturnsTrue |
								 ImGuiInputTextFlags_AutoSelectAll);

		// WARNING: The Escape poll is scoped to this field. An unscoped one would also catch an Escape aimed at
		// a modal elsewhere.
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
				m_Pending = PendingAction{.Action = PendingAction::Kind::RenameFolder, .Folder = path, .Name = typed};
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

		if (ImGui::Button("Remove", ImVec2(kConfirmButtonWidth, 0.0f)))
		{
			const PendingAction action{.Action = PendingAction::Kind::RemoveProject, .Project = m_RemoveCandidate};

			if (const Core::Result<void> applied = Apply(action); !applied)
			{
				ReportEditFailure(applied.GetError());
			}

			m_RemoveCandidate.clear();
			m_RemoveCandidateName.clear();
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();

		if (ImGui::Button("Cancel", ImVec2(kConfirmButtonWidth, 0.0f)))
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

		if (action.Action == PendingAction::Kind::AddProject)
		{
			m_Workspace.GetDialog().Start(
				PickProjectFile, [this, action = std::move(action)](Core::Result<std::filesystem::path> picked) mutable {
					OnProjectFilePicked(std::move(action), std::move(picked));
				});
			return;
		}

		ApplyAndReport(action);
	}

	void SolutionTreeView::OnProjectFilePicked(PendingAction action, Core::Result<std::filesystem::path> picked)
	{
		if (!picked)
		{
			APP_ERROR("Cannot open the file picker: {}", picked.GetError().Message);
			m_Events.Publish(
				ApplicationToast(Core::ToastKind::Error, "Cannot open file picker", picked.GetError().Message));
			return;
		}

		if (picked.GetValue().empty())
		{
			return;
		}

		action.Project = std::move(picked).GetValue();
		ApplyAndReport(action);
	}

	void SolutionTreeView::ReportEditFailure(const Core::Error& error)
	{
		APP_ERROR("{}", error.Message);
		m_Events.Publish(
			FolderToast(Core::ToastKind::Error, "Solution edit failed", error.Message, m_Workspace.GetRoot()));
	}

	void SolutionTreeView::ApplyAndReport(const PendingAction& action)
	{
		if (const Core::Result<void> applied = Apply(action); !applied)
		{
			ReportEditFailure(applied.GetError());
			return;
		}

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

		Core::Result<Core::Scope<SolutionEditor>> editor = SolutionEditor::Open(m_Workspace.GetLoadedSolution()->Path);

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