#include "Explorer/FileTreeView.h"

#include "Core/ImGui/UiScale.h"
#include "Core/Base/Log.h"
#include "Core/FileSystem/FileOperations.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/ContextMenu.h"
#include "Core/ImGui/EmptyState.h"
#include "Core/ImGui/Icons.h"
#include "Core/Platform/Shell.h"
#include "Core/Text/DocumentSet.h"

#include "Editor/StatusBar/ClipboardCopy.h"
#include "Terminal/TerminalRules.h"
#include "Explorer/FileRow.h"
#include "Explorer/TreeReveal.h"
#include "UI/IconButton.h"

#include "Core/Events/EventBus.h"

#include <IconsMaterialDesignIcons.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {
		float FitWidthToText(const char* text, float paddingX)
		{
			constexpr float kMinFieldWidth = 90.0f;

			// One character of slack leaves room for the caret as the name grows.
			const float fitted =
				ImGui::CalcTextSize(text).x + paddingX * 2.0f + ImGui::GetFontSize();

			return std::clamp(fitted, Core::Scaled(kMinFieldWidth), ImGui::GetContentRegionAvail().x);
		}
	}

	FileTreeView::FileTreeView(Workspace& workspace, const Core::DocumentSet& documents,
							   Core::EventBus& events)
		: m_Workspace(workspace), m_Documents(documents), m_Events(events)
	{
	}

	void FileTreeView::Render()
	{
		if (!m_Workspace.IsOpen())
		{
			ForgetRenames();
			m_KnownRoot.clear();
			RenderEmptyState();
			return;
		}

		// A different folder has none of the paths a rename or a deferred open was about.
		if (m_Workspace.GetRoot() != m_KnownRoot)
		{
			ForgetRenames();
			m_KnownRoot = m_Workspace.GetRoot();
		}

		FollowActiveDocument();
		ResolvePendingRename();
		RenderToolbar();
		ImGui::Separator();

		if (ImGui::BeginChild("##fileTreeBody", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
							  ImGuiWindowFlags_HorizontalScrollbar))
		{
			Core::FileNode& root = m_Workspace.GetTree().GetRoot();
			RenderNode(root);

			// The root's own name matching shows its row but no file, so it is the children
			// that decide, by the same test that draws them.
			if (m_Filter.IsActive() && !AnyChildPassesFilter(root))
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
				ImGui::TextWrapped("No files match \"%s\"", m_Filter.InputBuf);
				ImGui::PopStyleColor();
			}

			RenderBackgroundMenu();
		}

		ImGui::EndChild();

		// Whatever the walk did not reach is hidden by the filter or by the ignored-file
		// setting; left in place it would open or scroll to a row long after it was asked for.
		m_ForceOpen.clear();
		m_ScrollToPath.clear();

		// Outside the walk: RenderNode recurses over the tree by reference, so the executor
		// must not mutate it before the walk ends, and the modal must not be nested inside
		// the child window.
		ExecutePendingAction();
		RenderDeleteConfirmation();
	}

	void FileTreeView::FollowActiveDocument()
	{
		// Every change of the active document, however it came about: a go-to-definition
		// into another folder, a tab switch, a file opened from search. A file the user
		// collapsed the folder of stays collapsed until another document becomes active.
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

		if (path.empty())
		{
			return;
		}

		std::vector<std::filesystem::path> opened = ExpandToward(m_Workspace.GetTree(), path);

		if (opened.empty())
		{
			return;
		}

		m_ForceOpen.insert(m_ForceOpen.end(), opened.begin(), opened.end());
		m_ScrollToPath = path;
	}

	void FileTreeView::RenderToolbar()
	{
		if (ImGui::Button(ICON_MDI_REFRESH))
		{
			m_Workspace.Reload();
			Core::FileNode& root = m_Workspace.GetTree().GetRoot();

			if (root.Accessible)
			{
				APP_INFO("File tree refreshed");
			}
			else
			{
				APP_WARN("File tree refresh failed: the workspace folder is unreadable");
			}
		}

		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Refresh");
		}

		ImGui::SameLine();

		// The clear button takes its room from the box only while there is something to clear.
		const bool hasText = m_Filter.InputBuf[0] != '\0';
		const float clearWidth = IconButtonSize() + ImGui::GetStyle().ItemSpacing.x;
		ImGui::SetNextItemWidth(hasText ? -clearWidth : -1.0f);

		// Escape empties the box first and only then leaves it.
		if (ImGui::InputTextWithHint("##fileFilter", ICON_MDI_MAGNIFY "  Filter files",
									 m_Filter.InputBuf, IM_ARRAYSIZE(m_Filter.InputBuf),
									 ImGuiInputTextFlags_EscapeClearsAll))
		{
			m_Filter.Build();
		}

		if (hasText)
		{
			ImGui::SameLine();

			if (DrawIconButton("##fileFilterClear", ICON_MDI_CLOSE, false, "Clear filter"))
			{
				m_Filter.Clear();
			}
		}
	}

	void FileTreeView::RenderEmptyState()
	{
		// Greyed out while a dialog is open: the dialog runs on its own thread, so the
		// application stays clickable underneath it and only one may be open at a time.
		ImGui::BeginDisabled(m_Workspace.IsDialogOpen());
		const bool clicked = Core::EmptyState("No folder open.", "Open Folder");
		ImGui::EndDisabled();

		if (clicked)
		{
			OpenFolderInteractively(m_Workspace);
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
			if (const auto revealed = Core::Shell::RevealInFileBrowser(node.Path); !revealed)
			{
				APP_ERROR("{}", revealed.GetError().Message);
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

			// relative() fails when the two paths share no prefix; the absolute path is
			// still worth copying.
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
				RecordAction(PendingAction{PendingAction::Kind::Duplicate, node.Path, {}});
			}
		}

		const std::filesystem::path creationTarget =
			node.IsDirectory ? node.Path : node.Path.parent_path();

		ImGui::Separator();

		if (ImGui::MenuItem("New File"))
		{
			RecordAction(
				PendingAction{PendingAction::Kind::CreateFile, creationTarget, "new file.txt"});
		}

		if (ImGui::MenuItem("New Folder"))
		{
			RecordAction(
				PendingAction{PendingAction::Kind::MakeDirectory, creationTarget, "new folder"});
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
				RecordAction(PendingAction{PendingAction::Kind::Delete, node.Path, {}});
			}
		}

		if (node.IsDirectory)
		{
			ImGui::Separator();

			if (ImGui::MenuItem("Refresh"))
			{
				RecordAction(PendingAction{PendingAction::Kind::Refresh, node.Path, {}});
			}
		}

		ImGui::EndPopup();
	}

	void FileTreeView::RenderBackgroundMenu()
	{
		// The menu of the workspace folder, for the empty space under the rows - the same
		// handlers as that folder's own menu, so both do the same thing.
		//
		// Opened by hand for the reason Core::BeginRightClickPopup gives: the menu must not be
		// draggable, which BeginPopupContextWindow cannot ask for. Not over a row, whose own
		// menu answers there.
		constexpr const char* kPopupId = "##fileTreeBodyMenu";

		if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
			ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) && !ImGui::IsAnyItemHovered())
		{
			ImGui::OpenPopup(kPopupId);
		}

		if (!ImGui::BeginPopup(kPopupId, ImGuiWindowFlags_NoMove))
		{
			return;
		}

		const std::filesystem::path& root = m_Workspace.GetRoot();

		if (ImGui::MenuItem("New File"))
		{
			RecordAction(PendingAction{PendingAction::Kind::CreateFile, root, "new file.txt"});
		}

		if (ImGui::MenuItem("New Folder"))
		{
			RecordAction(PendingAction{PendingAction::Kind::MakeDirectory, root, "new folder"});
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Refresh"))
		{
			RecordAction(PendingAction{PendingAction::Kind::Refresh, root, {}});
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Show in Explorer"))
		{
			if (const auto revealed = Core::Shell::RevealInFileBrowser(root); !revealed)
			{
				APP_ERROR("{}", revealed.GetError().Message);
			}
		}

		if (ImGui::MenuItem("Open in Terminal"))
		{
			m_Workspace.RequestOpenTerminal(TerminalDirectoryFor(root, true));
		}

		ImGui::EndPopup();
	}

	void FileTreeView::StartRenaming(const Core::FileNode& node)
	{
		const std::string current = Core::ToUtf8(node.Path.filename());

		if (current.size() >= m_RenameBuffer.size())
		{
			APP_ERROR("Cannot rename '{}': the name is too long to edit", current);
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
			// The refresh that should have added it has not shown it; try again next frame.
			m_PendingRenamePath = path;
			return;
		}

		// A row the filter or the ignored-file setting hides never draws its editor, which
		// would leave the rename running unseen; the caller falls back to not renaming.
		if (IsReachable(path))
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

		// One retry only: a node still missing was removed or never created, and a stale
		// request must not start a rename long after the user moved on.
		if (const Core::FileNode* node = FindNode(path); node != nullptr && IsReachable(path))
		{
			StartRenaming(*node);

			if (m_RenamingPath == path)
			{
				return;
			}
		}

		// No rename will follow, so none is waited for.
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
		Core::FileNode* parent = FindNode(path.parent_path());

		if (parent != nullptr)
		{
			m_Workspace.GetTree().Refresh(*parent);
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
		m_DeleteCandidate = action.Target;
		m_OpenDeleteConfirmation = true;
	}

	void FileTreeView::HandleRefresh(const PendingAction& action)
	{
		if (Core::FileNode* node = FindNode(action.Target); node != nullptr)
		{
			m_Workspace.GetTree().Refresh(*node);
		}
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
			return action.Target;
		}

		const std::filesystem::path target =
			action.Target.parent_path() / Core::FromUtf8(action.Name);

		if (target == action.Target)
		{
			return action.Target;
		}

		if (const auto renamed = Core::Rename(action.Target, target); !renamed)
		{
			APP_ERROR("{}", renamed.GetError().Message);
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
		m_Workspace.RequestOpenFile(named, true);
	}

	void FileTreeView::HandleDuplicate(const PendingAction& action)
	{
		const auto copied = Core::Duplicate(action.Target);

		if (!copied)
		{
			APP_ERROR("{}", copied.GetError().Message);
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
			return;
		}

		const std::filesystem::path target =
			Core::MakeUniquePath(action.Target / Core::FromUtf8(action.Name));

		const auto created = action.Action == PendingAction::Kind::CreateFile
								 ? Core::CreateEmptyFile(target)
								 : Core::MakeDirectory(target);

		if (!created)
		{
			APP_ERROR("{}", created.GetError().Message);
			return;
		}

		if (Core::FileNode* parent = FindNode(action.Target); parent != nullptr)
		{
			ReadFolderFromDisk(*parent);
			parent->Expanded = true;
			m_ForceOpen.push_back(action.Target);
		}

		APP_INFO("Created '{}'", Core::ToUtf8(target));

		// Named by the user next, in place: the default name is a placeholder.
		StartRenamingPath(target);

		if (action.Action != PendingAction::Kind::CreateFile)
		{
			return;
		}

		// Opening focuses the new tab, which would take the keyboard from the name field and
		// end the rename before the first key; the file opens once it has its name. With no
		// rename to wait for, there is nothing to wait for.
		m_OpenWhenNamed = target;

		if (m_RenamingPath != target && m_PendingRenamePath != target)
		{
			OpenIfJustCreated(target, target);
		}
	}

	void FileTreeView::ReadFolderFromDisk(Core::FileNode& folder)
	{
		if (!folder.Loaded)
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
		if (m_OpenDeleteConfirmation)
		{
			ImGui::OpenPopup("Delete##fileTreeDelete");
			m_OpenDeleteConfirmation = false;
		}

		if (!ImGui::BeginPopupModal("Delete##fileTreeDelete", nullptr,
									ImGuiWindowFlags_AlwaysAutoResize))
		{
			return;
		}

		ImGui::Text("Move \"%s\" to the Recycle Bin?",
					Core::ToUtf8(m_DeleteCandidate.filename()).c_str());
		ImGui::Spacing();

		if (ImGui::Button("Move"))
		{
			if (const auto removed = Core::MoveToRecycleBin(m_DeleteCandidate); !removed)
			{
				APP_ERROR("{}", removed.GetError().Message);
			}
			else
			{
				RefreshParentOf(m_DeleteCandidate);
			}

			m_DeleteCandidate.clear();
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();

		if (ImGui::Button("Cancel"))
		{
			m_DeleteCandidate.clear();
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
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

		// A row's label sits GetTreeNodeToLabelSpacing right of where its arrow is or would
		// be; without the same shift the name jumps left the moment editing starts.
		constexpr float kFieldPaddingX = 2.0f;
		const float indent = ImGui::GetTreeNodeToLabelSpacing() - Core::Scaled(kFieldPaddingX);

		if (indent > 0.0f)
		{
			ImGui::Indent(indent);
		}

		// The theme's frame padding is sized for buttons and would make the edited row
		// taller than its neighbours.
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(Core::Scaled(kFieldPaddingX), 0.0f));
		ImGui::SetNextItemWidth(FitWidthToText(m_RenameBuffer.data(), Core::Scaled(kFieldPaddingX)));

		// The callback runs on every frame the field is active; the frame it activates on
		// selects the stem (AutoSelectAll would take the extension too).
		const auto selectStem = [](ImGuiInputTextCallbackData* data)
		{
			auto& self = *static_cast<FileTreeView*>(data->UserData);

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

		// Scoped to this field: an unscoped key poll would also catch an Escape aimed at a
		// modal elsewhere.
		const bool cancelled = ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape);

		// IsItemDeactivated fires whether or not the text changed, so a rename clicked away
		// from still ends instead of leaving the row stuck in the editor.
		const bool deactivated = !cancelled && ImGui::IsItemDeactivated();

		ImGui::PopStyleVar();

		if (indent > 0.0f)
		{
			ImGui::Unindent(indent);
		}

		if (cancelled)
		{
			m_RenamingPath.clear();
			OpenIfJustCreated(node.Path, node.Path);
		}
		else if (committed || deactivated)
		{
			RecordAction(PendingAction{PendingAction::Kind::Rename, node.Path, m_RenameBuffer.data()});
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

		// Read from the DocumentSet rather than from the workspace selection, which only
		// moves when a row here is clicked and so misses tab switches in the editor.
		const auto active = m_Documents.GetActive();
		const bool isActive = active.has_value() && m_Documents.At(*active).Path == node.Path;
		const bool isOpen = m_Documents.Find(node.Path).has_value();

		const std::string label =
			std::string{Core::IconForExtension(Core::ToUtf8(node.Path.extension()))} + "  " +
			node.Name;

		const FileRowLayout layout = LayoutFileRow(label);

		const std::string id = "##" + Core::ToUtf8(node.Path);

		// Only the active file is selected. An open file that is not active is told apart by
		// the accent colour of its name rather than by a background, which would read as a
		// second selected row.
		const bool clicked = ImGui::Selectable(id.c_str(), isActive, ImGuiSelectableFlags_AllowDoubleClick,
											   ImVec2(layout.Width, 0.0f));

		ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);

		if (isOpen && !isActive)
		{
			// An ignored file is greyed; the accent is mixed into the grey so it stays
			// both dimmed and marked.
			constexpr float kAccentShare = 0.6f;
			const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
			const ImVec4 base = ImGui::GetStyleColorVec4(dimmed ? ImGuiCol_TextDisabled : ImGuiCol_Text);
			const float share = dimmed ? kAccentShare : 1.0f;

			ink = ImGui::GetColorU32(ImVec4(base.x + (accent.x - base.x) * share,
											base.y + (accent.y - base.y) * share,
											base.z + (accent.z - base.z) * share,
											base.w + (accent.w - base.w) * share));
		}

		DrawFileRowLabel(layout, ink, label);

		if (!m_ScrollToPath.empty() && m_ScrollToPath == node.Path)
		{
			// Only when off screen: a row already in view stays where the user put it.
			if (!ImGui::IsItemVisible())
			{
				ImGui::SetScrollHereY(0.5f);
			}

			m_ScrollToPath.clear();
		}

		if (clicked)
		{
			m_Workspace.SelectFile(node.Path);
			m_Workspace.RequestOpenFile(node.Path, ImGui::IsMouseDoubleClicked(0));
		}

		if (dimmed)
		{
			ImGui::PopStyleColor();
		}

		RenderContextMenu(node);
	}

	void FileTreeView::RenderDirectoryRow(Core::FileNode& node)
	{
		if (const auto forced = std::find(m_ForceOpen.begin(), m_ForceOpen.end(), node.Path);
			forced != m_ForceOpen.end())
		{
			ImGui::SetNextItemOpen(true);
			m_ForceOpen.erase(forced);
		}

		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
		if (node.Expanded)
		{
			flags |= ImGuiTreeNodeFlags_DefaultOpen;
		}

		const bool dimmed =
			!node.Accessible || (node.Ignored && m_GitIgnoredDisplay == GitIgnoredDisplay::Dim);

		if (dimmed)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		}

		const bool open =
			ImGui::TreeNodeEx(Core::ToUtf8(node.Path).c_str(), flags, "%s  %s",
							  node.Expanded ? ICON_MDI_FOLDER_OPEN : ICON_MDI_FOLDER,
							  node.Name.c_str());

		if (dimmed)
		{
			ImGui::PopStyleColor();

			if (!node.Accessible && ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("This folder could not be read.");
			}
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