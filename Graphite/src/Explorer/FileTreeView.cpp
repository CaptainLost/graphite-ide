#include "Explorer/FileTreeView.h"

#include "Core/ImGui/UiScale.h"
#include "Core/Base/Log.h"
#include "Core/FileSystem/FileName.h"
#include "Core/FileSystem/FileOperations.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/ContextMenu.h"
#include "Core/ImGui/EmptyState.h"
#include "Core/ImGui/Icons.h"
#include "Core/ImGui/LoadingState.h"
#include "Core/Platform/Shell.h"
#include "Core/Text/DocumentSet.h"

#include "Theme/Palettes.h"
#include "UI/Lists/Chevron.h"
#include "UI/ClipboardCopy.h"
#include "UI/Dialogs/DialogLayout.h"
#include "UI/Lists/FilterField.h"
#include "UI/Lists/ListRow.h"
#include "Terminal/TerminalRules.h"
#include "Explorer/FileRow.h"
#include "Explorer/FilteredTree.h"
#include "Explorer/TreeReveal.h"
#include "Core/ImGui/IconButton.h"
#include "UI/OpenGesture.h"
#include "UI/ToastEvent.h"

#include "Core/Events/EventBus.h"

#include <IconsMaterialDesignIcons.h>

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {
		float FitWidthToText(const char* text, float paddingX)
		{
			constexpr float kMinFieldWidth = 90.0f;

			const float caretSlack = ImGui::GetFontSize();
			const float fittedWidth = ImGui::CalcTextSize(text).x + paddingX * 2.0f + caretSlack;

			return std::clamp(fittedWidth, Core::Scaled(kMinFieldWidth), ImGui::GetContentRegionAvail().x);
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
		  m_Scheme(scheme), m_Listing(std::move(submitJob))
	{
		events.Subscribe<WorkspaceFilesChangedEvent>(
			[this](const WorkspaceFilesChangedEvent& changed) { NoteFilesChanged(changed); });
	}

	void FileTreeView::WithdrawQuestions()
	{
		m_DeleteQuestion.Withdraw();
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
		RenderToolbar();
		ImGui::Separator();

		if (ImGui::BeginChild("##fileTreeBody", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None))
		{
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

	void FileTreeView::RenderToolbar()
	{
		constexpr int kToolbarButtons = 1;
		constexpr bool kNotEngaged = false;

		if (DrawFilterField("##fileFilter", m_Filter.InputBuf, IM_ARRAYSIZE(m_Filter.InputBuf),
							{.Hint = "Filter files", .Width = FilterFieldWidthBefore(kToolbarButtons)}))
		{
			m_Filter.Build();
		}

		ImGui::SameLine();

		if (Core::DrawIconButton("##refreshTree", ICON_MDI_REFRESH, kNotEngaged, "Refresh"))
		{
			m_Workspace.Reload();
			m_Listing.NoteEverythingChanged();
			Core::FileNode& root = m_Workspace.GetTree().GetRoot();

			if (root.Readable)
			{
				APP_INFO("File tree refreshed");
			}
			else
			{
				APP_WARN("File tree refresh failed: the workspace folder is unreadable");
			}
		}
	}

	void FileTreeView::RenderEmptyState()
	{
		ImGui::BeginDisabled(m_Workspace.IsDialogOpen());
		const bool clicked = Core::EmptyState("No folder open.", "Open Folder");
		ImGui::EndDisabled();

		if (clicked)
		{
			OpenFolderInteractively(m_Workspace);
		}
	}

	void FileTreeView::RenderFilteredTree()
	{
		constexpr const char* kFilteredTreeScope = "##filteredTree";

		m_Listing.Update();

		if (!m_Listing.HasListing())
		{
			RenderSearching();
			return;
		}

		RebuildFilteredTreeWhenOutOfDate();

		// WARNING: The scope keeps the filtered rows' ids apart from the real tree's. ImGui stores a
		// folder's open state by id, so a folder opened by the filter would stay open after it.
		ImGui::PushID(kFilteredTreeScope);
		RenderNode(m_FilteredTree);
		ImGui::PopID();

		m_ReopenFilteredFolders = false;

		const bool nothingMatches = !AnyChildPassesFilter(m_FilteredTree);

		if (nothingMatches)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::TextWrapped("No files match \"%s\"", m_Filter.InputBuf);
			ImGui::PopStyleColor();
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
			RecordAction(PendingAction{
				.Action = PendingAction::Kind::CreateFile, .Target = creationTarget, .Name = "new file.txt"});
		}

		if (ImGui::MenuItem("New Folder"))
		{
			RecordAction(PendingAction{
				.Action = PendingAction::Kind::MakeDirectory, .Target = creationTarget, .Name = "new folder"});
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

		if (!Core::BeginRightClickMenu(kPopupId, releasedOverEmptySpace))
		{
			return;
		}

		const std::filesystem::path& root = m_Workspace.GetRoot();

		if (ImGui::MenuItem("New File"))
		{
			RecordAction(PendingAction{.Action = PendingAction::Kind::CreateFile, .Target = root, .Name = "new file.txt"});
		}

		if (ImGui::MenuItem("New Folder"))
		{
			RecordAction(PendingAction{.Action = PendingAction::Kind::MakeDirectory, .Target = root, .Name = "new folder"});
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

		if (current.size() >= m_RenameBuffer.size())
		{
			APP_ERROR("Cannot rename '{}': the name is too long to edit", current);
			ReportFailure(Core::ToastKind::Warning, "Cannot rename", std::format("'{}': the name is too long to edit", current));
			return;
		}

		if (m_OpenWhenNamed != node.Path)
		{
			m_OpenWhenNamed.clear();
		}

		m_RenamingPath = node.Path;
		m_FocusRenameField = true;
		m_RenameSpan = RenameSelection(current, node.IsDirectory);

		std::fill(m_RenameBuffer.begin(), m_RenameBuffer.end(), '\0');
		std::copy_n(current.begin(), current.size(), m_RenameBuffer.begin());
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
		m_OpenWhenNamed.clear();
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

			const bool renameStarted = m_RenamingPath == path;

			if (renameStarted)
			{
				return;
			}
		}

		OpenIfJustCreated(path, path);
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
		case PendingAction::Kind::CreateFile:
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
		m_OpenWhenNamed.clear();

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
		OpenIfJustCreated(action.Target, ApplyRename(action));
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

	void FileTreeView::OpenIfJustCreated(const std::filesystem::path& created,
										 const std::filesystem::path& named)
	{
		if (m_OpenWhenNamed.empty() || m_OpenWhenNamed != created)
		{
			return;
		}

		m_OpenWhenNamed.clear();

		constexpr bool kPermanentTab = true;

		m_Workspace.RequestOpenFile(named, kPermanentTab);
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

		const Core::Result<void> created = action.Action == PendingAction::Kind::CreateFile
											   ? Core::CreateEmptyFile(target)
											   : Core::MakeDirectory(target);

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

		if (action.Action != PendingAction::Kind::CreateFile)
		{
			return;
		}

		// WARNING: The file opens only once it has its name. Opening focuses the new tab, which
		// would take the keyboard from the name field and end the rename before the first key.
		m_OpenWhenNamed = target;

		const bool noRenameToWaitFor = m_RenamingPath != target && m_PendingRenamePath != target;

		if (noRenameToWaitFor)
		{
			OpenIfJustCreated(target, target);
		}
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

		if (m_FocusRenameField)
		{
			ImGui::SetKeyboardFocusHere();
			m_FocusRenameField = false;
		}

		constexpr float kFieldPaddingX = 2.0f;
		const float indentToLabel = ImGui::GetTreeNodeToLabelSpacing() - Core::Scaled(kFieldPaddingX);
		const bool isIndented = indentToLabel > 0.0f;

		if (isIndented)
		{
			ImGui::Indent(indentToLabel);
		}

		constexpr float kNoFramePaddingY = 0.0f;

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(Core::Scaled(kFieldPaddingX), kNoFramePaddingY));
		ImGui::SetNextItemWidth(FitWidthToText(m_RenameBuffer.data(), Core::Scaled(kFieldPaddingX)));

		// WARNING: ImGui runs this on every frame the field is active. Only the frame it activates
		// on selects the stem, because AutoSelectAll would take the extension too.
		const auto selectStem = [](ImGuiInputTextCallbackData* data) {
			FileTreeView& self = *static_cast<FileTreeView*>(data->UserData);

			if (data->EventActivated)
			{
				const int end = std::min(static_cast<int>(self.m_RenameSpan.End), data->BufTextLen);
				const int start = std::min(static_cast<int>(self.m_RenameSpan.Start), end);
				data->SetSelection(start, end);
			}

			return 0;
		};

		const bool committed =
			ImGui::InputText("##renameField", m_RenameBuffer.data(), m_RenameBuffer.size(),
							 ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackAlways,
							 selectStem, this);

		// WARNING: Escape is read only while this field is focused. An unscoped key poll would
		// also catch an Escape aimed at a modal elsewhere.
		const bool cancelled = ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape);

		// WARNING: IsItemDeactivated fires whether or not the text changed, which is what ends a
		// rename that was clicked away from instead of leaving the row stuck in the editor.
		const bool deactivated = !cancelled && ImGui::IsItemDeactivated();

		ImGui::PopStyleVar();

		if (isIndented)
		{
			ImGui::Unindent(indentToLabel);
		}

		if (cancelled)
		{
			m_RenamingPath.clear();
			OpenIfJustCreated(node.Path, node.Path);
		}
		else if (committed || deactivated)
		{
			RecordAction(PendingAction{
				.Action = PendingAction::Kind::Rename, .Target = node.Path, .Name = m_RenameBuffer.data()});
			m_RenamingPath.clear();
		}

		return true;
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

		const std::string label =
			std::string{Core::IconForExtension(Core::ToUtf8(node.Path.extension()))} + "  " +
			node.Name;

		const FileRowLayout layout = LayoutFileRow();

		const std::string id = "##" + Core::ToUtf8(node.Path);

		const bool clicked = ListRowSelectable(id.c_str(), isShownInEditor, ImGuiSelectableFlags_AllowDoubleClick);

		// WARNING: The lookup builds a path key and an expanded tree can hold thousands of rows,
		// so only a row on screen is looked up.
		if (ImGui::IsItemVisible())
		{
			FileRowLook look{.Ink = ImGui::GetColorU32(ImGuiCol_Text), .Tab = OpenTabOf(m_Documents, node.Path)};

			look.Status = FileRowStatusOf(m_GitDecorations, node.Path, node.Ignored);

			if (look.Status.Decoration != GitDecoration::None)
			{
				look.Ink = look.Status.Ink;
			}

			DrawFileRowLabel(layout, look, label);
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

		const std::string label =
			std::string{node.Expanded ? ICON_MDI_FOLDER_OPEN : ICON_MDI_FOLDER} + "  " + node.Name;
		DrawTreeRowLabel(layout, label, folderStatus.HasMarker());

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

			ImGui::TreePop();
		}
		else if (node.Expanded)
		{
			m_Workspace.GetTree().Collapse(node);
		}
	}
}