#include "Explorer/FileTreeView.h"

#include "Core/Base/Log.h"
#include "Core/FileSystem/FileName.h"
#include "Core/FileSystem/AtomicWrite.h"
#include "Core/FileSystem/FileOperations.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/ContextMenu.h"
#include "Core/ImGui/EmptyState.h"
#include "Core/ImGui/LoadingState.h"
#include "Core/Platform/Shell.h"
#include "Core/Text/DocumentSet.h"
#include "Core/UI/PanelMarginScope.h"

#include "Theme/Palettes.h"
#include "UI/Lists/Chevron.h"
#include "UI/ClipboardCopy.h"
#include "UI/Dialogs/DialogLayout.h"
#include "UI/Dialogs/FileDialogOffer.h"
#include "UI/FileIcons.h"
#include "UI/Lists/FilterField.h"
#include "UI/Lists/ListRow.h"
#include "Terminal/TerminalRules.h"
#include "Explorer/FileRow.h"
#include "Explorer/FileTemplates.h"
#include "Explorer/NewFilePath.h"
#include "Explorer/FilteredTree.h"
#include "Explorer/TreeReveal.h"
#include "UI/OpenGesture.h"
#include "UI/TypographicMarks.h"
#include "UI/ToastEvent.h"

#include "Core/Events/EventBus.h"

#include <imgui_internal.h>

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {

		constexpr const char* kFilterFieldId = "##fileFilter";
		constexpr const char* kRefreshingFileList = "Refreshing the file list";

		std::string NewFromTemplateLabel()
		{
			return std::string{"New File from Template"} + std::string{kEllipsis};
		}

		void DrawTreeLoader(const std::string& text, Core::WhileLoaderWaits whileWaiting)
		{
			const float inset = TreeRowChevronBoxLeft();

			ImGui::Indent(inset);
			Core::LoadingLine(text.c_str(), whileWaiting);
			ImGui::Unindent(inset);
		}
	}

	void FileTreeView::ReportInvalidName(std::string title, const std::string& name)
	{
		const bool nameWasEmptiedToCancel = name.empty();

		if (nameWasEmptiedToCancel)
		{
			return;
		}

		ReportFailure(Core::ToastKind::Error, std::move(title), std::format("'{}' is not a valid file name", name));
	}

	void FileTreeView::ReportFailure(Core::ToastKind kind, std::string title, std::string_view message)
	{
		const std::filesystem::path& folderTheFileIsIn = m_Workspace.GetRoot();

		m_Events.Publish(FolderToast(kind, std::move(title), message, folderTheFileIsIn));
	}

	FileTreeView::FileTreeView(Workspace& workspace, const Core::DocumentSet& documents,
							   Core::EventBus& events, ModalGate& modals, const ActiveScheme& scheme,
							   Core::JobSubmitter submitJob)
		: m_Workspace(workspace), m_Documents(documents), m_Events(events), m_DeleteQuestion(modals, kDeleteTitle),
		  m_NewFileQuestion(modals, kNewFileTitle), m_Scheme(scheme), m_Listing(std::move(submitJob))
	{
		events.Subscribe<WorkspaceFilesChangedEvent>(
			[this](const WorkspaceFilesChangedEvent& changed) { NoteFilesChanged(changed); });
	}

	void FileTreeView::WithdrawQuestions()
	{
		m_DeleteQuestion.Withdraw();
		m_NewFileQuestion.Withdraw();
	}

	void FileTreeView::NoteFilesChanged(const WorkspaceFilesChangedEvent& changed)
	{
		if (changed.Everything)
		{
			m_Listing.NoteEverythingChanged();
		}
		else
		{
			m_Listing.NoteChangedDirectories(changed.Directories);
		}
	}

	void FileTreeView::Render()
	{
		// WARNING: The questions go with the folder. Begin, the only other code that hands the modal
		// gate back, is not reached past the early return.
		if (!m_Workspace.IsOpen())
		{
			ForgetRenames();
			WithdrawQuestions();
			m_KnownRoot.clear();
			m_Listing.SetRoot({});
			RenderEmptyState();
			return;
		}

		const bool isDifferentFolder = m_Workspace.GetRoot() != m_KnownRoot;

		if (isDifferentFolder)
		{
			ForgetRenames();
			WithdrawQuestions();
			m_KnownRoot = m_Workspace.GetRoot();
			m_Listing.SetRoot(m_KnownRoot);
		}

		// WARNING: Not followed while filtering. A document revealed in a tree that is not drawn
		// would have its folders close again, so it is revealed once the filter is cleared.
		if (!m_Filter.IsActive())
		{
			FollowActiveDocument();
		}

		ResolvePendingRename();
		const bool typingInFilter = RenderFilter();

		// WARNING: Read from the moment the filter is focused, so the list is fresh by the first letter, and never in
		// the background on a change on disk: every entry read costs a git ignore check.
		if (typingInFilter || m_Filter.IsActive())
		{
			m_Listing.Update();
		}

		if (ImGui::BeginChild("##fileTreeBody", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None))
		{
			const ListBodyScope listBody;

			if (m_Filter.IsActive())
			{
				RenderFilteredTree();
			}
			else
			{
				m_FilteredTreeText.clear();
				RenderNode(m_Workspace.GetTree().GetRoot());
			}

			RenderBackgroundMenu();
		}

		ImGui::EndChild();

		m_ForceOpen.clear();
		m_ScrollToPath.clear();

		// WARNING: Outside the walk. RenderNode recurses over the tree by reference, so the action
		// must not change it before the walk ends, and the modal must not sit inside the child window.
		ExecutePendingAction();
		RenderDeleteConfirmation();
		RenderNewFileQuestion();
	}

	void FileTreeView::FollowActiveDocument()
	{
		if (!m_FollowsActiveFile)
		{
			m_FollowedPath.clear();
			return;
		}

		const std::filesystem::path* activePath = m_Documents.GetActiveItemPath();
		const std::filesystem::path path = activePath != nullptr ? *activePath : std::filesystem::path{};
		const bool activeDocumentAlreadyFollowed = path == m_FollowedPath;

		if (activeDocumentAlreadyFollowed)
		{
			return;
		}

		m_FollowedPath = path;

		if (path.empty())
		{
			return;
		}

		const std::vector<std::filesystem::path> foldersOpenedOnTheWay = ExpandToward(m_Workspace.GetTree(), path);

		if (foldersOpenedOnTheWay.empty())
		{
			return;
		}

		m_ForceOpen.insert(m_ForceOpen.end(), foldersOpenedOnTheWay.begin(), foldersOpenedOnTheWay.end());
		m_ScrollToPath = path;
	}

	bool FileTreeView::RenderFilter()
	{
		const Core::PanelMarginScope margin;

		if (DrawFilterField(kFilterFieldId, m_Filter.InputBuf, IM_ARRAYSIZE(m_Filter.InputBuf), {.Hint = "Filter files"}))
		{
			m_Filter.Build();
		}

		return ImGui::GetActiveID() == ImGui::GetID(kFilterFieldId);
	}

	void FileTreeView::NewFileInRoot()
	{
		BeginNewFileIn(m_Workspace.GetRoot());
	}

	void FileTreeView::NewFolderInRoot()
	{
		MakeFolderIn(m_Workspace.GetRoot());
	}

	void FileTreeView::Refresh()
	{
		m_Workspace.Reload();
		m_Listing.NoteEverythingChanged();

		if (!m_Workspace.IsOpen())
		{
			return;
		}

		if (m_Workspace.GetTree().GetRoot().Readable)
		{
			APP_INFO("File tree refreshed");
		}
		else
		{
			APP_WARN("File tree refresh failed: the workspace folder is unreadable");
		}
	}

	void FileTreeView::RenderEmptyState()
	{
		const Core::PanelMarginScope margin;

		const ImVec2 fillTheRegion{0.0f, 0.0f};

		ImGui::BeginDisabled(m_Workspace.IsDialogOpen());
		const bool clicked = Core::EmptyState("No folder open.", "Open Folder", fillTheRegion, kFileDialogOpenNote);
		ImGui::EndDisabled();

		if (clicked)
		{
			OpenFolderInteractively(m_Workspace);
		}
	}

	void FileTreeView::RenderFilteredTree()
	{
		constexpr const char* kFilteredTreeScope = "##filteredTree";

		if (!m_Listing.HasListing())
		{
			RenderSearching();
			return;
		}

		RebuildFilteredTreeWhenOutOfDate();

		const ListingFreshness freshness =
			m_Listing.IsUpToDate() ? ListingFreshness::UpToDate : ListingFreshness::Refreshing;
		const bool nothingMatches = !AnyChildPassesFilter(m_FilteredTree);

		if (freshness == ListingFreshness::Refreshing && !nothingMatches)
		{
			DrawTreeLoader(kRefreshingFileList, Core::WhileLoaderWaits::ShowNothing);
		}

		// WARNING: The scope keeps the filtered rows' ids apart from the real tree's. ImGui stores a
		// folder's open state by id, so a folder opened by the filter would stay open after it.
		ImGui::PushID(kFilteredTreeScope);
		RenderNode(m_FilteredTree);
		ImGui::PopID();

		m_ReopenFilteredFolders = false;

		if (!nothingMatches)
		{
			return;
		}

		const std::string message = NoMatchingFilesText(m_Filter.InputBuf, freshness);

		switch (freshness)
		{
		case ListingFreshness::UpToDate:
			DrawTreeMessage(message);
			return;
		case ListingFreshness::Refreshing:
			DrawTreeLoader(message, Core::WhileLoaderWaits::ShowText);
			return;
		}
	}

	void FileTreeView::RenderSearching()
	{
		Core::LoadingState("Searching");
	}

	void FileTreeView::RebuildFilteredTreeWhenOutOfDate()
	{
		const std::string filterText = m_Filter.InputBuf;
		const bool filterTextChanged = filterText != m_FilteredTreeText;
		const bool listingChanged = m_Listing.GetVersion() != m_FilteredTreeListingVersion;

		if (!filterTextChanged && !listingChanged)
		{
			return;
		}

		m_FilteredTree = BuildFilteredTree(m_Workspace.GetTree().GetRoot(), m_Listing.GetEntries(),
										   [this](const ListedEntry& entry) { return m_Filter.PassFilter(entry.Name.c_str()); });
		m_FilteredTreeText = filterText;
		m_FilteredTreeListingVersion = m_Listing.GetVersion();

		if (filterTextChanged)
		{
			m_ReopenFilteredFolders = true;
		}
	}

	bool FileTreeView::PassesFilter(const Core::FileNode& node) const
	{
		if (!m_Filter.IsActive())
		{
			return true;
		}

		if (m_Filter.PassFilter(node.Name.c_str()))
		{
			return true;
		}

		return AnyChildPassesFilter(node);
	}

	bool FileTreeView::AnyChildPassesFilter(const Core::FileNode& node) const
	{
		for (const Core::FileNode& child : node.Children)
		{
			if (IsRowShown(child))
			{
				return true;
			}
		}

		return false;
	}

	bool FileTreeView::IsRowShown(const Core::FileNode& node) const
	{
		if (node.Ignored && m_GitIgnoredDisplay == GitIgnoredDisplay::Hide)
		{
			return false;
		}

		return PassesFilter(node);
	}

	void FileTreeView::RenderContextMenu(const Core::FileNode& node)
	{
		const std::string popupId = "##ctx" + Core::ToUtf8(node.Path);
		const ListPopupScope popup;

		if (!Core::BeginRightClickPopup(popupId.c_str()))
		{
			return;
		}

		const bool isRoot = m_Workspace.IsOpen() && node.Path == m_Workspace.GetRoot();

		if (ImGui::MenuItem("Show in Explorer"))
		{
			if (const Core::Result<void> revealed = Core::Shell::RevealInFileBrowser(node.Path); !revealed)
			{
				APP_ERROR("{}", revealed.GetError().Message);
				ReportFailure(Core::ToastKind::Error, "Cannot reveal file", revealed.GetError().Message);
			}
		}

		if (ImGui::MenuItem("Open in Terminal"))
		{
			m_Workspace.RequestOpenTerminal(TerminalDirectoryFor(node.Path, node.IsDirectory));
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Copy Path"))
		{
			CopyToClipboard(m_Events, "path", Core::ToUtf8(node.Path));
		}

		if (ImGui::MenuItem("Copy Relative Path"))
		{
			std::error_code code;
			const std::filesystem::path relative =
				std::filesystem::relative(node.Path, m_Workspace.GetRoot(), code);

			CopyToClipboard(m_Events, "relative path", Core::ToUtf8(code ? node.Path : relative));
		}

		if (ImGui::MenuItem(node.IsDirectory ? "Copy Folder Name" : "Copy File Name"))
		{
			CopyToClipboard(m_Events, node.IsDirectory ? "folder name" : "file name",
							Core::ToUtf8(node.Path.filename()));
		}

		if (!isRoot)
		{
			ImGui::Separator();

			if (ImGui::MenuItem("Duplicate"))
			{
				RecordAction(PendingAction{.Action = PendingAction::Kind::Duplicate, .Target = node.Path});
			}
		}

		const std::filesystem::path creationTarget =
			node.IsDirectory ? node.Path : node.Path.parent_path();

		ImGui::Separator();

		if (ImGui::MenuItem("New File"))
		{
			BeginNewFileIn(creationTarget);
		}

		if (ImGui::MenuItem(NewFromTemplateLabel().c_str()))
		{
			RecordAction(PendingAction{.Action = PendingAction::Kind::AskNewFileFromTemplate, .Target = creationTarget});
		}

		if (ImGui::MenuItem("New Folder"))
		{
			MakeFolderIn(creationTarget);
		}

		if (!isRoot)
		{
			ImGui::Separator();

			if (ImGui::MenuItem("Rename"))
			{
				StartRenaming(node);
			}

			if (ImGui::MenuItem("Delete"))
			{
				RecordAction(PendingAction{.Action = PendingAction::Kind::Delete, .Target = node.Path});
			}
		}

		if (node.IsDirectory)
		{
			ImGui::Separator();

			if (ImGui::MenuItem("Refresh"))
			{
				RecordAction(PendingAction{.Action = PendingAction::Kind::Refresh, .Target = node.Path});
			}
		}

		ImGui::EndPopup();
	}

	void FileTreeView::RenderBackgroundMenu()
	{
		constexpr const char* kPopupId = "##fileTreeBodyMenu";

		const bool releasedOverEmptySpace = ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
											ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
											!ImGui::IsAnyItemHovered();
		const ListPopupScope popup;

		if (!Core::BeginRightClickMenu(kPopupId, releasedOverEmptySpace))
		{
			return;
		}

		const std::filesystem::path& root = m_Workspace.GetRoot();

		if (ImGui::MenuItem("New File"))
		{
			BeginNewFileIn(root);
		}

		if (ImGui::MenuItem(NewFromTemplateLabel().c_str()))
		{
			RecordAction(PendingAction{.Action = PendingAction::Kind::AskNewFileFromTemplate, .Target = root});
		}

		if (ImGui::MenuItem("New Folder"))
		{
			MakeFolderIn(root);
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Refresh"))
		{
			RecordAction(PendingAction{.Action = PendingAction::Kind::Refresh, .Target = root});
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Show in Explorer"))
		{
			if (const Core::Result<void> revealed = Core::Shell::RevealInFileBrowser(root); !revealed)
			{
				APP_ERROR("{}", revealed.GetError().Message);
				ReportFailure(Core::ToastKind::Error, "Cannot reveal file", revealed.GetError().Message);
			}
		}

		if (ImGui::MenuItem("Open in Terminal"))
		{
			constexpr bool kRootIsDirectory = true;

			m_Workspace.RequestOpenTerminal(TerminalDirectoryFor(root, kRootIsDirectory));
		}

		ImGui::EndPopup();
	}

	void FileTreeView::StartRenaming(const Core::FileNode& node)
	{
		const std::string current = Core::ToUtf8(node.Path.filename());

		if (!m_RenameField.Start(current, node.IsDirectory))
		{
			APP_ERROR("Cannot rename '{}': the name is too long to edit", current);
			ReportFailure(Core::ToastKind::Warning, "Cannot rename", std::format("'{}': the name is too long to edit", current));
			return;
		}

		m_RenamingPath = node.Path;
	}

	void FileTreeView::StartRenamingPath(const std::filesystem::path& path)
	{
		const Core::FileNode* node = FindNode(path);

		if (node == nullptr)
		{
			m_PendingRenamePath = path;
			return;
		}

		const bool editorWouldBeDrawn = IsReachable(path);

		if (editorWouldBeDrawn)
		{
			StartRenaming(*node);
		}
	}

	bool FileTreeView::IsReachable(const std::filesystem::path& path)
	{
		for (std::filesystem::path current = path; !current.empty(); current = current.parent_path())
		{
			const Core::FileNode* node = FindNode(current);

			if (node == nullptr || !IsRowShown(*node))
			{
				return false;
			}

			if (!m_Workspace.IsOpen() || current == m_Workspace.GetRoot() || current == current.parent_path())
			{
				return true;
			}
		}

		return false;
	}

	void FileTreeView::ForgetRenames()
	{
		m_RenamingPath.clear();
		m_PendingRenamePath.clear();
		m_NewFileIn.clear();
	}

	void FileTreeView::ResolvePendingRename()
	{
		if (m_PendingRenamePath.empty())
		{
			return;
		}

		const std::filesystem::path path = std::move(m_PendingRenamePath);
		m_PendingRenamePath.clear();

		if (const Core::FileNode* node = FindNode(path); node != nullptr && IsReachable(path))
		{
			StartRenaming(*node);
		}
	}

	Core::FileNode* FileTreeView::FindNode(const std::filesystem::path& path)
	{
		if (!m_Workspace.IsOpen())
		{
			return nullptr;
		}

		std::vector<Core::FileNode*> stack{&m_Workspace.GetTree().GetRoot()};

		while (!stack.empty())
		{
			Core::FileNode* node = stack.back();
			stack.pop_back();

			if (node->Path == path)
			{
				return node;
			}

			for (Core::FileNode& child : node->Children)
			{
				stack.push_back(&child);
			}
		}

		return nullptr;
	}

	void FileTreeView::RefreshParentOf(const std::filesystem::path& path)
	{
		Core::FileNode* parentFolder = FindNode(path.parent_path());

		if (parentFolder != nullptr)
		{
			m_Workspace.GetTree().Refresh(*parentFolder);
		}
	}

	void FileTreeView::RecordAction(PendingAction action)
	{
		if (m_Pending.Action != PendingAction::Kind::None)
		{
			APP_WARN("Discarding a pending file action that was replaced before it ran");
		}

		m_Pending = std::move(action);
	}

	void FileTreeView::BeginNewFileIn(const std::filesystem::path& folder)
	{
		RecordAction(PendingAction{.Action = PendingAction::Kind::BeginNewFile, .Target = folder});
	}

	void FileTreeView::MakeFolderIn(const std::filesystem::path& folder)
	{
		RecordAction(PendingAction{.Action = PendingAction::Kind::MakeDirectory, .Target = folder, .Name = "new folder"});
	}

	void FileTreeView::ExecutePendingAction()
	{
		const PendingAction action = m_Pending;
		m_Pending = PendingAction{};

		switch (action.Action)
		{
		case PendingAction::Kind::None:
			return;
		case PendingAction::Kind::Delete:
			HandleDelete(action);
			return;
		case PendingAction::Kind::Refresh:
			HandleRefresh(action);
			return;
		case PendingAction::Kind::Rename:
			HandleRename(action);
			return;
		case PendingAction::Kind::Duplicate:
			HandleDuplicate(action);
			return;
		case PendingAction::Kind::BeginNewFile:
			HandleBeginNewFile(action);
			return;
		case PendingAction::Kind::AskNewFileFromTemplate:
			m_NewFileQuestion.AskOnce(NewFileQuestion{.Folder = action.Target});
			return;
		case PendingAction::Kind::CreateNamedFile:
			HandleCreateNamedFile(action);
			return;
		case PendingAction::Kind::MakeDirectory:
			HandleCreate(action);
			return;
		default:
			return;
		}
	}

	void FileTreeView::HandleDelete(const PendingAction& action)
	{
		m_RenamingPath.clear();

		const DeleteQuestion question{.Target = action.Target};

		m_DeleteQuestion.AskOnce(question);
	}

	void FileTreeView::HandleRefresh(const PendingAction& action)
	{
		if (Core::FileNode* node = FindNode(action.Target); node != nullptr)
		{
			m_Workspace.GetTree().Refresh(*node);
		}

		m_Listing.NoteChangedDirectories({action.Target});
	}

	void FileTreeView::HandleRename(const PendingAction& action)
	{
		static_cast<void>(ApplyRename(action));
	}

	std::filesystem::path FileTreeView::ApplyRename(const PendingAction& action)
	{
		if (!Core::IsValidFileName(action.Name))
		{
			APP_ERROR("'{}' is not a valid file name", action.Name);
			ReportInvalidName("Rename failed", action.Name);
			return action.Target;
		}

		const std::filesystem::path target =
			action.Target.parent_path() / Core::FromUtf8(action.Name);

		if (target == action.Target)
		{
			return action.Target;
		}

		if (const Core::Result<void> renamed = Core::Rename(action.Target, target); !renamed)
		{
			APP_ERROR("{}", renamed.GetError().Message);
			ReportFailure(Core::ToastKind::Error, "Rename failed", renamed.GetError().Message);
			return action.Target;
		}

		RefreshParentOf(action.Target);
		return target;
	}

	void FileTreeView::HandleDuplicate(const PendingAction& action)
	{
		const Core::Result<std::filesystem::path> copied = Core::Duplicate(action.Target);

		if (!copied)
		{
			APP_ERROR("{}", copied.GetError().Message);
			ReportFailure(Core::ToastKind::Error, "Duplicate failed", copied.GetError().Message);
			return;
		}

		RefreshParentOf(action.Target);
		StartRenamingPath(copied.GetValue());
	}

	void FileTreeView::HandleCreate(const PendingAction& action)
	{
		if (!Core::IsValidFileName(action.Name))
		{
			APP_ERROR("'{}' is not a valid file name", action.Name);
			ReportInvalidName("Create failed", action.Name);
			return;
		}

		const std::filesystem::path target =
			Core::MakeUniquePath(action.Target / Core::FromUtf8(action.Name));

		const Core::Result<void> created = Core::MakeDirectory(target);

		if (!created)
		{
			APP_ERROR("{}", created.GetError().Message);
			ReportFailure(Core::ToastKind::Error, "Create failed", created.GetError().Message);
			return;
		}

		if (Core::FileNode* parent = FindNode(action.Target); parent != nullptr)
		{
			ReadFolderFromDisk(*parent);
			parent->Expanded = true;
			m_ForceOpen.push_back(action.Target);
		}

		APP_INFO("Created '{}'", Core::ToUtf8(target));

		StartRenamingPath(target);
	}

	void FileTreeView::HandleBeginNewFile(const PendingAction& action)
	{
		Core::FileNode* folder = FindNode(action.Target);

		if (folder == nullptr || !folder->IsDirectory)
		{
			return;
		}

		ReadFolderFromDisk(*folder);
		folder->Expanded = true;
		m_ForceOpen.push_back(action.Target);

		ContinueNamingNewFile(action.Target, {});
	}

	void FileTreeView::ContinueNamingNewFile(const std::filesystem::path& folder, std::string_view typed)
	{
		constexpr bool kNotAFolder = false;

		if (m_NewFileField.Start(typed, kNotAFolder))
		{
			m_RenamingPath.clear();
			m_NewFileIn = folder;
		}
	}

	void FileTreeView::HandleCreateNamedFile(const PendingAction& action)
	{
		const std::optional<NewFilePath> path = ParseNewFilePath(action.Name);

		if (!path.has_value())
		{
			APP_ERROR("'{}' is not a valid file name", action.Name);
			ReportInvalidName("Create failed", action.Name);
			ContinueNamingNewFile(action.Target, action.Name);
			return;
		}

		const std::vector<FileToCreate> files{FileToCreate{.Path = *path, .Content = {}}};

		if (const std::optional<std::string> problem = NewFileProblem(action.Target, files); problem.has_value())
		{
			ReportFailure(Core::ToastKind::Error, "Create failed", *problem);
			ContinueNamingNewFile(action.Target, action.Name);
			return;
		}

		if (const Core::Result<std::filesystem::path> created = CreateFiles(action.Target, files); !created)
		{
			APP_ERROR("{}", created.GetError().Message);
			ReportFailure(Core::ToastKind::Error, "Create failed", created.GetError().Message);
			ContinueNamingNewFile(action.Target, action.Name);
		}
	}

	std::optional<std::string> FileTreeView::NewFileProblem(const std::filesystem::path& folder,
															const std::vector<FileToCreate>& files) const
	{
		for (const FileToCreate& file : files)
		{
			std::filesystem::path target = folder;

			for (const std::string& piece : file.Path.Folders)
			{
				target /= Core::FromUtf8(piece);
			}

			target /= Core::FromUtf8(file.Path.File);

			std::error_code code;

			if (std::filesystem::exists(target, code))
			{
				return std::format("'{}' already exists", file.Path.File);
			}
		}

		return std::nullopt;
	}

	std::vector<FileTreeView::FileToCreate> FileTreeView::FilesFromTemplate(const std::filesystem::path& folder,
																		   FileTemplate fileTemplate,
																		   std::string_view name) const
	{
		std::vector<FileToCreate> files;

		if (fileTemplate == FileTemplate::Empty)
		{
			if (const std::optional<NewFilePath> path = ParseNewFilePath(name); path.has_value())
			{
				files.push_back(FileToCreate{.Path = *path, .Content = {}});
			}

			return files;
		}

		std::vector<std::string> foldersBelowRoot;

		std::error_code code;
		const std::filesystem::path relative = std::filesystem::relative(folder, m_Workspace.GetRoot(), code);

		if (!code && relative != ".")
		{
			for (const std::filesystem::path& piece : relative)
			{
				foldersBelowRoot.push_back(Core::ToUtf8(piece));
			}
		}

		const std::string csharpNamespace =
			CSharpNamespaceFor(foldersBelowRoot, Core::ToUtf8(m_Workspace.GetRoot().filename()));

		for (TemplateFile& file : ExpandTemplate(fileTemplate, name, csharpNamespace))
		{
			files.push_back(FileToCreate{.Path = NewFilePath{.Folders = {}, .File = std::move(file.Name)},
										 .Content = std::move(file.Content)});
		}

		return files;
	}

	Core::Result<std::filesystem::path> FileTreeView::CreateFiles(const std::filesystem::path& folder,
																  const std::vector<FileToCreate>& files)
	{
		std::filesystem::path first;

		for (const FileToCreate& file : files)
		{
			std::filesystem::path directory = folder;

			for (const std::string& piece : file.Path.Folders)
			{
				directory /= Core::FromUtf8(piece);

				std::error_code code;

				if (!std::filesystem::exists(directory, code))
				{
					if (const Core::Result<void> made = Core::MakeDirectory(directory); !made)
					{
						return made.GetError();
					}
				}
			}

			const std::filesystem::path target = directory / Core::FromUtf8(file.Path.File);

			// WARNING: The empty file claims the name first, and fails if the name is taken. Writing the content
			// atomically replaces whatever is there, so it must never be the first thing to touch the path.
			if (const Core::Result<void> claimed = Core::CreateEmptyFile(target); !claimed)
			{
				return claimed.GetError();
			}

			if (!file.Content.empty())
			{
				if (const Core::Result<void> written = Core::WriteFileAtomically(target, file.Content); !written)
				{
					return written.GetError();
				}
			}

			APP_INFO("Created '{}'", Core::ToUtf8(target));

			if (first.empty())
			{
				first = target;
			}
		}

		if (Core::FileNode* parent = FindNode(folder); parent != nullptr)
		{
			ReadFolderFromDisk(*parent);
			parent->Expanded = true;
		}

		const std::vector<std::filesystem::path> foldersOpenedOnTheWay = ExpandToward(m_Workspace.GetTree(), first);

		m_ForceOpen.insert(m_ForceOpen.end(), foldersOpenedOnTheWay.begin(), foldersOpenedOnTheWay.end());
		m_ScrollToPath = first;
		m_Listing.NoteChangedDirectories({folder});

		constexpr bool kPermanentTab = true;

		m_Workspace.RequestOpenFile(first, kPermanentTab);

		return first;
	}

	void FileTreeView::ReadFolderFromDisk(Core::FileNode& folder)
	{
		if (!folder.ChildrenLoaded)
		{
			// Expand fails only on a file node; an unreadable folder it logs and marks itself.
			static_cast<void>(m_Workspace.GetTree().Expand(folder));
		}
		else
		{
			m_Workspace.GetTree().Refresh(folder);
		}
	}

	void FileTreeView::RenderDeleteConfirmation()
	{
		DialogLayout::BeforeBegin(DialogLayout::kNarrowWidth);
		const DeleteQuestion* asked = m_DeleteQuestion.Begin();
		DialogLayout::AfterBegin();

		if (asked == nullptr)
		{
			return;
		}

		// WARNING: Copied out before Answer(), which drops the payload `asked` points into.
		const std::filesystem::path target = asked->Target;

		DialogLayout::Text(std::format("Move \"{}\" to the Recycle Bin?", Core::ToUtf8(target.filename())));
		DialogLayout::Gap();

		// WARNING: Destructive, not safe: Enter must not delete, and a drive with no Recycle Bin deletes
		// for good.
		const DialogLayout::ButtonSpec absentButton{};
		const DialogLayout::Choice choice =
			DialogLayout::ButtonRow(absentButton, {.Label = "Move"}, DangerColourOf(m_Scheme.Get()));

		if (choice == DialogLayout::Choice::Destructive)
		{
			m_DeleteQuestion.Answer();

			if (const Core::Result<void> removed = Core::MoveToRecycleBin(target); !removed)
			{
				APP_ERROR("{}", removed.GetError().Message);
				ReportFailure(Core::ToastKind::Error, "Delete failed", removed.GetError().Message);
			}
			else
			{
				RefreshParentOf(target);
			}
		}
		else if (choice == DialogLayout::Choice::Cancel)
		{
			m_DeleteQuestion.Answer();
		}

		m_DeleteQuestion.End();
	}

	bool FileTreeView::RenderRenameEditor(const Core::FileNode& node)
	{
		if (m_RenamingPath != node.Path)
		{
			return false;
		}

		switch (m_RenameField.Draw("##renameField"))
		{
		case RenameEnd::StillEditing:
			break;

		case RenameEnd::Cancelled:
			m_RenamingPath.clear();
			break;

		case RenameEnd::Committed:
			RecordAction(PendingAction{.Action = PendingAction::Kind::Rename, .Target = node.Path, .Name = m_RenameField.Text()});
			m_RenamingPath.clear();
			break;
		}

		return true;
	}

	void FileTreeView::RenderNewFileEditor(const Core::FileNode& folder)
	{
		if (m_NewFileIn != folder.Path)
		{
			return;
		}

		switch (m_NewFileField.Draw("##newFileField"))
		{
		case RenameEnd::StillEditing:
			break;

		case RenameEnd::Cancelled:
			m_NewFileIn.clear();
			break;

		case RenameEnd::Committed:
		{
			const std::string typed = m_NewFileField.Text();

			m_NewFileIn.clear();

			// WARNING: A name emptied to nothing cancels, like Escape. No file has been made yet.
			if (!typed.empty())
			{
				RecordAction(PendingAction{.Action = PendingAction::Kind::CreateNamedFile, .Target = folder.Path, .Name = typed});
			}

			break;
		}
		}
	}

	void FileTreeView::RenderNewFileQuestion()
	{
		DialogLayout::BeforeBegin(DialogLayout::kNarrowWidth);
		const NewFileQuestion* asked = m_NewFileQuestion.Begin();
		DialogLayout::AfterBegin();

		if (asked == nullptr)
		{
			return;
		}

		// WARNING: Copied out before Answer(), which drops the payload `asked` points into.
		const std::filesystem::path folder = asked->Folder;

		const bool isFirstFrameOfQuestion = ImGui::IsWindowAppearing();

		if (isFirstFrameOfQuestion)
		{
			m_NewFileName.fill('\0');
			m_NewFileTemplate = 0;
		}

		std::error_code code;
		const std::filesystem::path relative = std::filesystem::relative(folder, m_Workspace.GetRoot(), code);
		const bool folderIsRoot = code || relative == ".";
		const std::string folderName =
			Core::ToUtf8(folderIsRoot ? m_Workspace.GetRoot().filename() : relative);

		DialogLayout::Text(std::format("New file in {}", folderName));
		DialogLayout::Gap();

		const FileTemplate fileTemplate = kFileTemplates[static_cast<std::size_t>(m_NewFileTemplate)];

		// WARNING: The labels are string literals, so their data() ends in a null as ImGui needs.
		ImGui::SetNextItemWidth(-FLT_MIN);

		if (ImGui::BeginCombo("##newFileTemplate", TemplateLabel(fileTemplate).data()))
		{
			for (std::size_t index = 0; index < kFileTemplates.size(); ++index)
			{
				const bool isCurrent = static_cast<int>(index) == m_NewFileTemplate;

				if (ImGui::Selectable(TemplateLabel(kFileTemplates[index]).data(), isCurrent))
				{
					m_NewFileTemplate = static_cast<int>(index);
				}
			}

			ImGui::EndCombo();
		}

		if (isFirstFrameOfQuestion)
		{
			ImGui::SetKeyboardFocusHere();
		}

		ImGui::SetNextItemWidth(-FLT_MIN);
		const bool entered = ImGui::InputTextWithHint("##newFileName", TemplateNameHint(fileTemplate).data(),
													  m_NewFileName.data(), m_NewFileName.size(),
													  ImGuiInputTextFlags_EnterReturnsTrue);

		const std::string name = m_NewFileName.data();
		std::optional<std::string> problem = TemplateNameProblem(fileTemplate, name);
		std::vector<FileToCreate> files;

		if (!problem.has_value() && !name.empty())
		{
			files = FilesFromTemplate(folder, fileTemplate, name);

			if (files.empty())
			{
				problem = std::format("'{}' is not a valid file name", name);
			}
			else
			{
				problem = NewFileProblem(folder, files);
			}
		}

		if (problem.has_value())
		{
			DialogLayout::ErrorText(*problem, DangerColourOf(m_Scheme.Get()));
		}

		DialogLayout::Gap();

		const bool acceptable = !name.empty() && !problem.has_value();
		const DialogLayout::ButtonSpec absentButton{};

		const DialogLayout::Choice choice =
			DialogLayout::ButtonRow({.Label = "Create", .Enabled = acceptable}, absentButton, DangerColourOf(m_Scheme.Get()));

		if (acceptable && (choice == DialogLayout::Choice::Safe || entered))
		{
			m_NewFileQuestion.Answer();

			if (const Core::Result<std::filesystem::path> created = CreateFiles(folder, files); !created)
			{
				APP_ERROR("{}", created.GetError().Message);
				ReportFailure(Core::ToastKind::Error, "Create failed", created.GetError().Message);
			}
		}
		else if (choice == DialogLayout::Choice::Cancel)
		{
			m_NewFileQuestion.Answer();
		}

		m_NewFileQuestion.End();
	}

	void FileTreeView::RenderNode(Core::FileNode& node)
	{
		if (!IsRowShown(node))
		{
			return;
		}

		if (RenderRenameEditor(node))
		{
			return;
		}

		if (node.IsDirectory)
		{
			RenderDirectoryRow(node);
		}
		else
		{
			RenderFileRow(node);
		}
	}

	void FileTreeView::RenderFileRow(const Core::FileNode& node)
	{
		const bool dimmed = node.Ignored && m_GitIgnoredDisplay == GitIgnoredDisplay::Dim;

		if (dimmed)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		}

		const bool isShownInEditor = !m_ShownFile.empty() && m_ShownFile == node.Path;

		const FileRowLayout layout = LayoutFileRow();

		const std::string id = "##" + Core::ToUtf8(node.Path);

		const bool clicked = ListRowSelectable(id.c_str(), isShownInEditor, ImGuiSelectableFlags_AllowDoubleClick);

		// WARNING: The lookup builds a path key and an expanded tree can hold thousands of rows,
		// so only a row on screen is looked up.
		if (ImGui::IsItemVisible())
		{
			const IconTone tone = dimmed ? IconTone::Dimmed : IconTone::Normal;

			FileRowLook look{.Ink = ListRowNameColour(isShownInEditor && !dimmed),
							 .Tab = OpenTabOf(m_Documents, node.Path),
							 .Icon = FileIconOf(KindOfFile(node.Path), tone)};

			look.Status = FileRowStatusOf(m_GitDecorations, node.Path, node.Ignored);

			if (look.Status.Decoration != GitDecoration::None)
			{
				look.Ink = look.Status.Ink;
			}

			DrawFileRowLabel(layout, look, node.Name);
		}

		if (!m_ScrollToPath.empty() && m_ScrollToPath == node.Path)
		{
			constexpr float kCentreOfView = 0.5f;

			if (!ImGui::IsItemVisible())
			{
				ImGui::SetScrollHereY(kCentreOfView);
			}

			m_ScrollToPath.clear();
		}

		if (clicked)
		{
			const bool opensInPermanentTab = OpensPermanently();

			m_Workspace.RequestOpenFile(node.Path, opensInPermanentTab);
		}

		if (dimmed)
		{
			ImGui::PopStyleColor();
		}

		RenderContextMenu(node);
	}

	void FileTreeView::RenderDirectoryRow(Core::FileNode& node)
	{
		if (m_ReopenFilteredFolders)
		{
			ImGui::SetNextItemOpen(node.Expanded);
		}

		const std::vector<std::filesystem::path>::iterator forced =
			std::find(m_ForceOpen.begin(), m_ForceOpen.end(), node.Path);

		if (forced != m_ForceOpen.end())
		{
			constexpr bool kOpenTheFolder = true;

			ImGui::SetNextItemOpen(kOpenTheFolder);
			m_ForceOpen.erase(forced);
		}

		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
		if (node.Expanded)
		{
			flags |= ImGuiTreeNodeFlags_DefaultOpen;
		}

		const bool dimmed =
			!node.Readable || (node.Ignored && m_GitIgnoredDisplay == GitIgnoredDisplay::Dim);

		if (dimmed)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		}

		const FileRowLayout layout = LayoutFileRow();
		const bool open = ChevronTreeNode(Core::ToUtf8(node.Path).c_str(), flags);

		const RowStatus folderStatus =
			node.Ignored ? RowStatus{} : FolderRowStatusOf(m_GitDecorations, node.Path);

		const IconTone tone = dimmed ? IconTone::Dimmed : IconTone::Normal;
		DrawTreeRowLabel(layout, FolderIconOf(open, tone), node.Name, folderStatus.HasMarker());

		if (dimmed)
		{
			ImGui::PopStyleColor();

			if (!node.Readable && ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("This folder could not be read.");
			}
		}

		if (!node.Ignored && ImGui::IsItemVisible())
		{
			DrawRowMarker(folderStatus, layout);
		}

		RenderContextMenu(node);

		if (open)
		{
			if (!node.Expanded)
			{
				// A directory row, so Expand cannot fail; an unreadable folder it logs and marks.
				static_cast<void>(m_Workspace.GetTree().Expand(node));
			}

			for (Core::FileNode& child : node.Children)
			{
				RenderNode(child);
			}

			RenderNewFileEditor(node);

			ImGui::TreePop();
		}
		else if (node.Expanded)
		{
			m_Workspace.GetTree().Collapse(node);
		}
	}
}