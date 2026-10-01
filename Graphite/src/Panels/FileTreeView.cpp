#include "Panels/FileTreeView.h"

#include "Core/ImGui/UiScale.h"
#include "Core/Base/Log.h"
#include "Core/Fs/FileOperations.h"
#include "Core/Fs/FileSystem.h"
#include "Core/ImGui/ContextMenu.h"
#include "Core/ImGui/EmptyState.h"
#include "Core/ImGui/Icons.h"
#include "Core/Platform/Shell.h"
#include "Core/Text/DocumentSet.h"

#include "Panels/TerminalRules.h"

#include <IconsMaterialDesignIcons.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {
		void PushFadedHighlight()
		{
			constexpr float kFadedAlpha = 0.45f;

			ImVec4 colour = ImGui::GetStyleColorVec4(ImGuiCol_Header);
			colour.w *= kFadedAlpha;
			ImGui::PushStyleColor(ImGuiCol_Header, colour);
		}

		float FitWidthToText(const char* text, float paddingX)
		{
			constexpr float kMinFieldWidth = 90.0f;

			// One character of slack leaves room for the caret as the name grows.
			const float fitted =
				ImGui::CalcTextSize(text).x + paddingX * 2.0f + ImGui::GetFontSize();

			return std::clamp(fitted, Core::Scaled(kMinFieldWidth), ImGui::GetContentRegionAvail().x);
		}
	}

	FileTreeView::FileTreeView(Workspace& workspace, const Core::DocumentSet& documents)
		: m_Workspace(workspace), m_Documents(documents)
	{
	}

	void FileTreeView::Render()
	{
		if (!m_Workspace.IsOpen())
		{
			RenderEmptyState();
			return;
		}

		RenderToolbar();
		ImGui::Separator();

		if (ImGui::BeginChild("##fileTreeBody", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
							  ImGuiWindowFlags_HorizontalScrollbar))
		{
			RenderNode(m_Workspace.Tree().GetRoot());
		}

		ImGui::EndChild();

		// Outside the walk: RenderNode recurses over the tree by reference, so the executor
		// must not mutate it before the walk ends, and the modal must not be nested inside
		// the child window.
		ExecutePendingAction();
		RenderDeleteConfirmation();
	}

	void FileTreeView::RenderToolbar()
	{
		if (ImGui::Button(ICON_MDI_REFRESH))
		{
			m_Workspace.Reload();
			Core::FileNode& root = m_Workspace.Tree().GetRoot();

			if (root.Accessible)
				APP_INFO("File tree refreshed");
			else
				APP_WARN("File tree refresh failed: the workspace folder is unreadable");
		}

		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Refresh");

		ImGui::SameLine();
		ImGui::SetNextItemWidth(-1.0f);

		if (ImGui::InputTextWithHint("##fileFilter", ICON_MDI_MAGNIFY "  Filter files",
									 m_Filter.InputBuf, IM_ARRAYSIZE(m_Filter.InputBuf)))
		{
			m_Filter.Build();
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
			OpenFolderInteractively(m_Workspace);
	}

	bool FileTreeView::PassesFilter(const Core::FileNode& node) const
	{
		if (!m_Filter.IsActive())
			return true;

		if (m_Filter.PassFilter(node.Name.c_str()))
			return true;

		return AnyChildPassesFilter(node);
	}

	bool FileTreeView::AnyChildPassesFilter(const Core::FileNode& node) const
	{
		for (const Core::FileNode& child : node.Children)
		{
			if (PassesFilter(child))
				return true;
		}

		return false;
	}

	void FileTreeView::RenderContextMenu(const Core::FileNode& node)
	{
		const std::string popupId = "##ctx" + Core::ToUtf8(node.Path);

		if (!Core::BeginRightClickPopup(popupId.c_str()))
			return;

		const bool isRoot = m_Workspace.IsOpen() && node.Path == m_Workspace.GetRoot();

		if (ImGui::MenuItem("Show in Explorer"))
		{
			if (const auto revealed = Core::Shell::RevealInFileBrowser(node.Path); !revealed)
				APP_ERROR("{}", revealed.GetError().Message);
		}

		if (ImGui::MenuItem("Open in Terminal"))
		{
			m_Workspace.RequestOpenTerminal(TerminalDirectoryFor(node.Path, node.IsDirectory));
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Copy Path"))
			ImGui::SetClipboardText(Core::ToUtf8(node.Path).c_str());

		if (ImGui::MenuItem("Copy Relative Path"))
		{
			std::error_code code;
			const std::filesystem::path relative =
				std::filesystem::relative(node.Path, m_Workspace.GetRoot(), code);

			// relative() fails when the two paths share no prefix; the absolute path is
			// still worth copying.
			ImGui::SetClipboardText(Core::ToUtf8(code ? node.Path : relative).c_str());
		}

		if (!isRoot)
		{
			ImGui::Separator();

			if (ImGui::MenuItem("Duplicate"))
				RecordAction(PendingAction{PendingAction::Kind::Duplicate, node.Path, {}});
		}

		const std::filesystem::path creationTarget =
			node.IsDirectory ? node.Path : node.Path.parent_path();

		ImGui::Separator();

		if (ImGui::MenuItem("New File"))
			RecordAction(
				PendingAction{PendingAction::Kind::CreateFile, creationTarget, "new file.txt"});

		if (ImGui::MenuItem("New Folder"))
			RecordAction(
				PendingAction{PendingAction::Kind::MakeDirectory, creationTarget, "new folder"});

		if (!isRoot)
		{
			ImGui::Separator();

			if (ImGui::MenuItem("Rename"))
				StartRenaming(node);

			if (ImGui::MenuItem("Delete"))
				RecordAction(PendingAction{PendingAction::Kind::Delete, node.Path, {}});
		}

		if (node.IsDirectory)
		{
			ImGui::Separator();

			if (ImGui::MenuItem("Refresh"))
				RecordAction(PendingAction{PendingAction::Kind::Refresh, node.Path, {}});
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

		m_RenamingPath = node.Path;
		m_FocusRenameField = true;

		std::fill(m_RenameBuffer.begin(), m_RenameBuffer.end(), '\0');
		std::copy_n(current.begin(), current.size(), m_RenameBuffer.begin());
	}

	Core::FileNode* FileTreeView::FindNode(const std::filesystem::path& path)
	{
		if (!m_Workspace.IsOpen())
			return nullptr;

		std::vector<Core::FileNode*> stack{&m_Workspace.Tree().GetRoot()};

		while (!stack.empty())
		{
			Core::FileNode* node = stack.back();
			stack.pop_back();

			if (node->Path == path)
				return node;

			for (Core::FileNode& child : node->Children)
				stack.push_back(&child);
		}

		return nullptr;
	}

	void FileTreeView::RefreshParentOf(const std::filesystem::path& path)
	{
		Core::FileNode* parent = FindNode(path.parent_path());

		if (parent != nullptr)
			m_Workspace.Tree().Refresh(*parent);
	}

	void FileTreeView::RecordAction(PendingAction action)
	{
		if (m_Pending.kind != PendingAction::Kind::None)
		{
			APP_WARN("Discarding a pending file action that was replaced before it ran");
		}

		m_Pending = std::move(action);
	}

	void FileTreeView::ExecutePendingAction()
	{
		const PendingAction action = m_Pending;
		m_Pending = PendingAction{};

		switch (action.kind)
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
		m_DeleteCandidate = action.target;
		m_OpenDeleteConfirmation = true;
	}

	void FileTreeView::HandleRefresh(const PendingAction& action)
	{
		if (Core::FileNode* node = FindNode(action.target); node != nullptr)
			m_Workspace.Tree().Refresh(*node);
	}

	void FileTreeView::HandleRename(const PendingAction& action)
	{
		if (!Core::IsValidFileName(action.name))
		{
			APP_ERROR("'{}' is not a valid file name", action.name);
			return;
		}

		const std::filesystem::path target =
			action.target.parent_path() / Core::FromUtf8(action.name);

		if (target == action.target)
			return;

		if (const auto renamed = Core::Rename(action.target, target); !renamed)
		{
			APP_ERROR("{}", renamed.GetError().Message);
			return;
		}

		RefreshParentOf(action.target);
	}

	void FileTreeView::HandleDuplicate(const PendingAction& action)
	{
		const auto copied = Core::Duplicate(action.target);

		if (!copied)
		{
			APP_ERROR("{}", copied.GetError().Message);
			return;
		}

		RefreshParentOf(action.target);
	}

	void FileTreeView::HandleCreate(const PendingAction& action)
	{
		if (!Core::IsValidFileName(action.name))
		{
			APP_ERROR("'{}' is not a valid file name", action.name);
			return;
		}

		const std::filesystem::path target =
			Core::MakeUniquePath(action.target / Core::FromUtf8(action.name));

		const auto created = action.kind == PendingAction::Kind::CreateFile
								 ? Core::CreateEmptyFile(target)
								 : Core::MakeDirectory(target);

		if (!created)
		{
			APP_ERROR("{}", created.GetError().Message);
			return;
		}

		if (Core::FileNode* parent = FindNode(action.target); parent != nullptr)
		{
			ReadFolderFromDisk(*parent);
			parent->Expanded = true;
			m_ForceOpenPath = action.target;
		}

		APP_INFO("Created '{}'", Core::ToUtf8(target));
	}

	void FileTreeView::ReadFolderFromDisk(Core::FileNode& folder)
	{
		if (!folder.Loaded)
		{
			// Expand fails only on a file node; an unreadable folder it logs and marks itself.
			static_cast<void>(m_Workspace.Tree().Expand(folder));
		}
		else
		{
			m_Workspace.Tree().Refresh(folder);
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
				APP_ERROR("{}", removed.GetError().Message);
			else
				RefreshParentOf(m_DeleteCandidate);

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
			return false;

		if (m_FocusRenameField)
		{
			ImGui::SetKeyboardFocusHere();
			m_FocusRenameField = false;
		}

		// A directory's label sits GetTreeNodeToLabelSpacing right of its arrow; without
		// the same shift the name jumps left the moment editing starts. Files have no
		// arrow and need no shift.
		constexpr float kFieldPaddingX = 2.0f;
		const float indent =
			node.IsDirectory ? ImGui::GetTreeNodeToLabelSpacing() - Core::Scaled(kFieldPaddingX) : 0.0f;

		if (indent > 0.0f)
			ImGui::Indent(indent);

		// The theme's frame padding is sized for buttons and would make the edited row
		// taller than its neighbours.
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(Core::Scaled(kFieldPaddingX), 0.0f));
		ImGui::SetNextItemWidth(FitWidthToText(m_RenameBuffer.data(), Core::Scaled(kFieldPaddingX)));

		const bool committed =
			ImGui::InputText("##renameField", m_RenameBuffer.data(), m_RenameBuffer.size(),
							 ImGuiInputTextFlags_EnterReturnsTrue |
								 ImGuiInputTextFlags_AutoSelectAll);

		// Scoped to this field: an unscoped key poll would also catch an Escape aimed at a
		// modal elsewhere.
		const bool cancelled = ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape);

		// IsItemDeactivated fires whether or not the text changed, so a rename clicked away
		// from still ends instead of leaving the row stuck in the editor.
		const bool deactivated = !cancelled && ImGui::IsItemDeactivated();

		ImGui::PopStyleVar();

		if (indent > 0.0f)
			ImGui::Unindent(indent);

		if (cancelled)
		{
			m_RenamingPath.clear();
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
		if (!PassesFilter(node))
			return;

		if (node.Ignored && m_GitIgnoredDisplay == GitIgnoredDisplay::Hide)
			return;

		if (RenderRenameEditor(node))
			return;

		if (node.IsDirectory)
			RenderDirectoryRow(node);
		else
			RenderFileRow(node);
	}

	void FileTreeView::RenderFileRow(const Core::FileNode& node)
	{
		const bool dimmed = node.Ignored && m_GitIgnoredDisplay == GitIgnoredDisplay::Dim;

		if (dimmed)
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

		// Read from the DocumentSet rather than from the workspace selection, which only
		// moves when a row here is clicked and so misses tab switches in the editor.
		const auto active = m_Documents.GetActive();
		const bool isActive = active.has_value() && m_Documents.At(*active).Path == node.Path;
		const bool isOpen = m_Documents.Find(node.Path).has_value();
		const bool faded = isOpen && !isActive;

		if (faded)
			PushFadedHighlight();

		const std::string label =
			std::string{Core::IconForExtension(Core::ToUtf8(node.Path.extension()))} + "  " +
			node.Name;

		const bool clicked = ImGui::Selectable(label.c_str(), isActive || faded,
											   ImGuiSelectableFlags_AllowDoubleClick);

		if (faded)
			ImGui::PopStyleColor();

		if (clicked)
		{
			m_Workspace.SelectFile(node.Path);
			m_Workspace.RequestOpenFile(node.Path, ImGui::IsMouseDoubleClicked(0));
		}

		if (dimmed)
			ImGui::PopStyleColor();

		RenderContextMenu(node);
	}

	void FileTreeView::RenderDirectoryRow(Core::FileNode& node)
	{
		if (!m_ForceOpenPath.empty() && m_ForceOpenPath == node.Path)
		{
			ImGui::SetNextItemOpen(true);
			m_ForceOpenPath.clear();
		}

		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
		if (node.Expanded)
			flags |= ImGuiTreeNodeFlags_DefaultOpen;

		const bool dimmed =
			!node.Accessible || (node.Ignored && m_GitIgnoredDisplay == GitIgnoredDisplay::Dim);

		if (dimmed)
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

		const bool open =
			ImGui::TreeNodeEx(Core::ToUtf8(node.Path).c_str(), flags, "%s  %s",
							  node.Expanded ? ICON_MDI_FOLDER_OPEN : ICON_MDI_FOLDER,
							  node.Name.c_str());

		if (dimmed)
		{
			ImGui::PopStyleColor();

			if (!node.Accessible && ImGui::IsItemHovered())
				ImGui::SetTooltip("This folder could not be read.");
		}

		RenderContextMenu(node);

		if (open)
		{
			if (!node.Expanded)
			{
				// A directory row, so Expand cannot fail; an unreadable folder it logs and marks.
				static_cast<void>(m_Workspace.Tree().Expand(node));
			}

			for (Core::FileNode& child : node.Children)
				RenderNode(child);

			ImGui::TreePop();
		}
		else if (node.Expanded)
		{
			m_Workspace.Tree().Collapse(node);
		}
	}
}