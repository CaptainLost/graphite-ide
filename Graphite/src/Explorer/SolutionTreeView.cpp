#include "Explorer/SolutionTreeView.h"

#include "Core/Base/Log.h"
#include "Core/Base/Strings.h"
#include "Core/Events/EventBus.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/ContextMenu.h"
#include "Core/ImGui/EmptyState.h"
#include "Core/ImGui/LoadingState.h"

#include "Core/Platform/FileDialog.h"
#include "Core/Text/DocumentSet.h"
#include "Core/UI/PanelMarginScope.h"
#include "Explorer/FileRow.h"
#include "Git/GitDecorationView.h"
#include "Solution/SolutionEditor.h"
#include "UI/Lists/Chevron.h"
#include "UI/Dialogs/DialogLayout.h"
#include "UI/Dialogs/FileDialogOffer.h"
#include "UI/FileIcons.h"
#include "UI/Lists/FilterField.h"
#include "UI/Lists/ListRow.h"
#include "UI/OpenGesture.h"
#include "UI/ToastEvent.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {
		const std::string kSolutionRootPath;
		constexpr const char* kReadingTheSolution = "Reading the solution";
		const std::string kReadingTheSolutionAgain = "Reading the solution again";
		constexpr const char* kNoShortcut = nullptr;
		constexpr bool kNotSelected = false;
		constexpr bool kOpenTheRow = true;
		constexpr float kHalf = 0.5f;

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
									   Core::EventBus& events, ModalGate& modals)
		: m_Workspace(workspace), m_Documents(documents), m_Events(events), m_RemoveQuestion(modals, kRemoveTitle)
	{
	}

	void SolutionTreeView::WithdrawQuestions()
	{
		m_RemoveQuestion.Withdraw();
	}

	void SolutionTreeView::Render()
	{
		m_RenameWhenListed.BeginFrame(m_Workspace.IsOpen() && CanEditSolution(),
									  m_Workspace.GetSolutionLoad());

		// WARNING: The question goes with the solution. Begin, the only other code that hands the modal
		// gate back, is not reached past the early return.
		if (!m_Workspace.IsOpen() || !m_Workspace.GetLoadedSolution())
		{
			WithdrawQuestions();
			RenderEmptyState();
			return;
		}

		if (!m_Filter.IsActive())
		{
			FollowActiveDocument();
		}

		RenderFilter();

		const Solution& solution = *m_Workspace.GetLoadedSolution();

		ForgetStaleGroupDecorations();
		ForgetStaleFilterMatches();

		if (ImGui::BeginChild("##solutionBody", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None))
		{
			const ListBodyScope listBody;
			constexpr const char* kFilteredTreeScope = "##filteredTree";
			const bool filtering = m_Filter.IsActive();

			if (m_Workspace.IsReadingSolution())
			{
				DrawTreeLoader(kReadingTheSolutionAgain, Core::WhileLoaderWaits::TakeNoRoom);
			}

			// WARNING: ImGui keeps a row's open state by its ID. Without a scope of their own, the rows the
			// filter opens would stay open once it is cleared.
			if (filtering)
			{
				ImGui::PushID(kFilteredTreeScope);
			}

			const std::string projectCount = "(" + Core::Counted(solution.ProjectCount, "project") + ")";

			if (OpensToward(solution.Root) || m_RenameWhenListed.IsLooking() || m_ReopenFilteredRows)
			{
				ImGui::SetNextItemOpen(kOpenTheRow);
			}

			// WARNING: The root's id is the solution's path: the same while projects come and go, another for another
			// solution, so one solution's open rows do not open in the next.
			const std::string rootId = "##" + Core::ToUtf8(solution.Path);
			const FileRowLayout layout = LayoutFileRow();
			const bool rootOpen =
				ChevronTreeNode(rootId.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
			DrawTreeRowLabel(layout, FileIconOf(KindOfFile(solution.Path)), solution.Name,
							 ImGui::IsItemVisible() && GroupDecorationOf(solution.Root) != GitDecoration::None,
							 projectCount);

			DrawGroupMarker(solution.Root, layout);
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
				DrawTreeMessage(std::format("No files match \"{}\"", m_Filter.InputBuf));
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
		const Core::PanelMarginScope margin;

		if (DrawFilterField("##solutionFilter", m_Filter.InputBuf, IM_ARRAYSIZE(m_Filter.InputBuf),
							{.Hint = "Filter files"}))
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

		if (m_ReopenFilteredRows)
		{
			ImGui::SetNextItemOpen(HoldsMatch(folder));
		}

		if (OpensToward(folder) || m_RenameWhenListed.LeadsTo(path))
		{
			ImGui::SetNextItemOpen(kOpenTheRow);
		}

		const FileRowLayout layout = LayoutFileRow();
		const bool isOpen =
			ChevronTreeNode(folder.Name.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
		DrawTreeRowLabel(layout, FolderIconOf(isOpen), folder.Name,
						 ImGui::IsItemVisible() && GroupDecorationOf(folder) != GitDecoration::None);

		DrawGroupMarker(folder, layout);
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

		const std::string shownName = project.Exists ? project.DisplayName : project.DisplayName + "  (missing)";
		const IconTone tone = project.Exists ? IconTone::Normal : IconTone::Dimmed;

		const std::string rowId = "##" + Core::ToUtf8(project.Path);

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

		const FileRowLayout layout = LayoutFileRow();
		const bool isOpen = ChevronTreeNode(rowId.c_str(), flags);
		DrawTreeRowLabel(layout, FileIconOf(KindOfFile(project.Path), tone), shownName,
						 ImGui::IsItemVisible() && GroupDecorationOf(project) != GitDecoration::None);

		if (!project.Exists)
		{
			ImGui::PopStyleColor();
		}

		DrawGroupMarker(project, layout);
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
		// WARNING: An id of its own, not the label: a content folder named References sits beside it.
		if (references.empty() || !ChevronTreeNode("##references", ImGuiTreeNodeFlags_SpanAvailWidth, "References"))
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
				if (m_ReopenFilteredRows)
				{
					ImGui::SetNextItemOpen(HoldsMatch(child));
				}

				if (OpensToward(child))
				{
					ImGui::SetNextItemOpen(kOpenTheRow);
				}

				const FileRowLayout layout = LayoutFileRow();
				const bool isOpen = ChevronTreeNode(child.Name.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
				constexpr bool kNoMarker = false;
				DrawTreeRowLabel(layout, FolderIconOf(isOpen), child.Name, kNoMarker);

				if (isOpen)
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

		std::string shownName = file.Name;

		if (!file.Exists)
		{
			shownName += " (missing)";
		}

		const FileRowLayout layout = LayoutFileRow();

		// WARNING: ImGui gives same-labelled siblings the same id, and two projects can hold files of the
		// same name, so the id is the full path.
		const std::string id = "##" + Core::ToUtf8(file.Path);

		const ImGuiSelectableFlags flags =
			ImGuiSelectableFlags_AllowDoubleClick | (file.Exists ? 0 : ImGuiSelectableFlags_Disabled);

		const bool clicked = ListRowSelectable(id.c_str(), isShownInEditor, flags);

		if (ImGui::IsItemVisible())
		{
			const IconTone tone = file.Exists ? IconTone::Normal : IconTone::Dimmed;

			FileRowLook rowLook{.Ink = file.Exists ? ListRowNameColour(isShownInEditor)
												   : ImGui::GetColorU32(ImGuiCol_TextDisabled),
								.Icon = FileIconOf(KindOfFile(file.Path), tone)};

			if (file.Exists)
			{
				rowLook.Tab = OpenTabOf(m_Documents, file.Path);
				rowLook.Status = FileRowStatusOf(m_GitDecorations, file.Path);

				if (rowLook.Status.Decoration != GitDecoration::None)
				{
					rowLook.Ink = rowLook.Status.Ink;
				}
			}

			DrawFileRowLabel(layout, rowLook, shownName);
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

	void SolutionTreeView::DrawGroupMarker(const Project& project, const FileRowLayout& layout)
	{
		if (ImGui::IsItemVisible())
		{
			DrawGroupMarker(GroupDecorationOf(project), layout);
		}
	}

	void SolutionTreeView::DrawGroupMarker(const Folder& folder, const FileRowLayout& layout)
	{
		if (ImGui::IsItemVisible())
		{
			DrawGroupMarker(GroupDecorationOf(folder), layout);
		}
	}

	void SolutionTreeView::DrawGroupMarker(GitDecoration decoration, const FileRowLayout& layout)
	{
		if (decoration == GitDecoration::None)
		{
			return;
		}

		RowStatus status;
		status.Decoration = decoration;
		status.Dot = true;
		status.Ink = m_GitDecorations.ColourFor(decoration);

		DrawRowMarker(status, layout);
	}

	void SolutionTreeView::RenderEmptyState()
	{
		const Core::PanelMarginScope margin;

		if (!m_Workspace.IsOpen())
		{
			Core::EmptyState("Open a folder to see its Visual Studio solution.");
			return;
		}

		if (m_Workspace.IsReadingSolution())
		{
			Core::LoadingState(kReadingTheSolution);
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
		const ListPopupScope popup;

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

		if (editable && OfferOpenFileDialog(m_Workspace.GetDialog()))
		{
			ImGui::CloseCurrentPopup();
		}

		if (ImGui::MenuItem("New Folder", kNoShortcut, kNotSelected, editable))
		{
			AddFolderIn(folder, path);
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

	void SolutionTreeView::NewFolderInRoot()
	{
		if (!CanEditSolution())
		{
			return;
		}

		AddFolderIn(m_Workspace.GetLoadedSolution()->Root, kSolutionRootPath);
	}

	void SolutionTreeView::AddFolderIn(const Folder& folder, const std::string& path)
	{
		m_Pending = PendingAction{.Action = PendingAction::Kind::AddFolder,
								  .Folder = ChildPath(path, UniqueFolderName(folder, "New Folder"))};
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
		constexpr bool kIsFolder = true;

		if (!m_RenameField.Start(folder.Name, kIsFolder))
		{
			APP_ERROR("Cannot rename '{}': the name is too long to edit", folder.Name);
			m_Events.Publish(FolderToast(Core::ToastKind::Warning, "Cannot rename",
										 std::format("'{}': the name is too long to edit", folder.Name), m_Workspace.GetRoot()));
			return;
		}

		m_RenamingFolder = path;
	}

	void SolutionTreeView::RenderContextMenu(const Project& project,
											 const std::string& folder)
	{
		const std::string popupId = "##ctxSlnProject" + Core::ToUtf8(project.Path);
		const ListPopupScope popup;

		if (!Core::BeginRightClickPopup(popupId.c_str()))
		{
			return;
		}

		const bool editable = CanEditSolution();

		if (ImGui::MenuItem("Remove from Solution", kNoShortcut, kNotSelected, editable))
		{
			const RemoveQuestion question{.Project = project.Path, .Name = project.DisplayName};

			m_RemoveQuestion.AskOnce(question);
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

		switch (m_RenameField.Draw("##renameSlnFolder"))
		{
		case RenameEnd::StillEditing:
			break;

		case RenameEnd::Cancelled:
			m_RenamingFolder.clear();
			break;

		case RenameEnd::Committed:
		{
			const std::string typed = m_RenameField.Text();

			if (!typed.empty() && typed != folder.Name)
			{
				m_Pending = PendingAction{.Action = PendingAction::Kind::RenameFolder, .Folder = path, .Name = typed};
			}

			m_RenamingFolder.clear();
			break;
		}
		}

		return true;
	}

	void SolutionTreeView::RenderRemoveConfirmation()
	{
		DialogLayout::BeforeBegin(DialogLayout::kNarrowWidth);
		const RemoveQuestion* asked = m_RemoveQuestion.Begin();
		DialogLayout::AfterBegin();

		if (asked == nullptr)
		{
			return;
		}

		// WARNING: Copied out before Answer(), which drops the payload `asked` points into.
		const PendingAction action{.Action = PendingAction::Kind::RemoveProject, .Project = asked->Project};

		DialogLayout::Text(std::format("Remove \"{}\" from the solution?", asked->Name));
		DialogLayout::Gap();
		DialogLayout::MutedText("Its files stay on disk — only the solution's reference is removed.");
		DialogLayout::Gap();

		const DialogLayout::Choice choice = DialogLayout::ButtonRow({.Label = "Remove"});

		if (choice == DialogLayout::Choice::Safe)
		{
			m_RemoveQuestion.Answer();

			if (const Core::Result<void> applied = Apply(action); !applied)
			{
				ReportEditFailure(applied.GetError());
			}
		}
		else if (choice == DialogLayout::Choice::Cancel)
		{
			m_RemoveQuestion.Answer();
		}

		m_RemoveQuestion.End();
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
				PickProjectFile,
				[this, action = std::move(action)](const std::filesystem::path& picked) mutable {
					action.Project = picked;
					ApplyAndReport(action);
				},
				[this](const Core::Error& error) { m_Workspace.ReportDialogFailure(error); });
			return;
		}

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
		const std::uint64_t loadBeforeTheEdit = m_Workspace.GetSolutionLoad();

		if (const Core::Result<void> applied = Apply(action); !applied)
		{
			ReportEditFailure(applied.GetError());
			return;
		}

		if (action.Action == PendingAction::Kind::AddFolder)
		{
			m_RenameWhenListed.Request(action.Folder, loadBeforeTheEdit);
		}
	}

	Core::Result<void> SolutionTreeView::Apply(const PendingAction& action)
	{
		Core::Result<void> saved = SaveEdit(action);

		if (saved)
		{
			m_Workspace.ReloadSolution();
		}

		return saved;
	}

	Core::Result<void> SolutionTreeView::SaveEdit(const PendingAction& action)
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