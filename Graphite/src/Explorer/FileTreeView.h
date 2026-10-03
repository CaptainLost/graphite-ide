#pragma once

#include "Workspace/GitIgnoredDisplay.h"
#include "Workspace/Workspace.h"
#include "Explorer/RenameSelection.h"

#include "Core/FileSystem/FileTree.h"

#include <imgui.h>

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace Core {

	class DocumentSet;
	class EventBus;

}

namespace Graphite {

	class FileTreeView final
	{
	public:
		FileTreeView(Workspace& workspace, const Core::DocumentSet& documents, Core::EventBus& events);

		void Render();

		// Off, the tree is left as the user arranged it; turned back on, it reveals the
		// active document straight away.
		void SetFollowsActiveFile(bool follow) { m_FollowsActiveFile = follow; }

		void SetGitIgnoredDisplay(GitIgnoredDisplay display) { m_GitIgnoredDisplay = display; }

	private:
		struct PendingAction
		{
			enum class Kind
			{
				None,
				Delete,
				Rename,
				CreateFile,
				MakeDirectory,
				Duplicate,
				Refresh
			};

			Kind Action = Kind::None;
			std::filesystem::path Target;
			std::string Name;
		};

		void FollowActiveDocument();
		void RenderToolbar();
		void RenderEmptyState();
		void RenderNode(Core::FileNode& node);
		void RenderFileRow(const Core::FileNode& node);
		void RenderDirectoryRow(Core::FileNode& node);
		void RenderContextMenu(const Core::FileNode& node);
		void RenderBackgroundMenu();
		void RenderDeleteConfirmation();
		bool RenderRenameEditor(const Core::FileNode& node);
		void StartRenaming(const Core::FileNode& node);
		void StartRenamingPath(const std::filesystem::path& path);
		void ResolvePendingRename();
		void CopyToClipboard(const std::string& text);
		void RecordAction(PendingAction action);
		void ExecutePendingAction();
		void HandleDelete(const PendingAction& action);
		void HandleRefresh(const PendingAction& action);
		void HandleRename(const PendingAction& action);
		std::filesystem::path ApplyRename(const PendingAction& action);
		void OpenIfJustCreated(const std::filesystem::path& created, const std::filesystem::path& named);
		void HandleDuplicate(const PendingAction& action);
		void HandleCreate(const PendingAction& action);
		void ReadFolderFromDisk(Core::FileNode& folder);
		Core::FileNode* FindNode(const std::filesystem::path& path);
		void RefreshParentOf(const std::filesystem::path& path);
		bool PassesFilter(const Core::FileNode& node) const;
		bool AnyChildPassesFilter(const Core::FileNode& node) const;

		Workspace& m_Workspace;
		const Core::DocumentSet& m_Documents;
		Core::EventBus& m_Events;
		ImGuiTextFilter m_Filter;
		PendingAction m_Pending;
		std::filesystem::path m_DeleteCandidate;
		bool m_OpenDeleteConfirmation = false;

		// Folders whose rows are opened the next time the walk reaches them: a folder a file
		// was just created in, and every folder on the way to a newly active document.
		std::vector<std::filesystem::path> m_ForceOpen;

		// The active document the tree last revealed, and the row to scroll into view in the
		// walk that follows the reveal.
		std::filesystem::path m_FollowedPath;
		bool m_FollowsActiveFile = true;
		std::filesystem::path m_ScrollToPath;

		std::filesystem::path m_RenamingPath;

		std::array<char, 1024> m_RenameBuffer{};
		bool m_FocusRenameField = false;

		// What the field selects when it opens, applied once by its callback.
		RenameSpan m_RenameSpan;
		bool m_ApplyRenameSpan = false;

		// A node just created or duplicated that the tree did not yet hold when its rename
		// was asked for; the rename starts on the first frame the node can be found.
		std::filesystem::path m_PendingRenamePath;

		// A file just created, opened in a permanent tab once its inline rename ends under
		// whatever name it was given.
		std::filesystem::path m_OpenWhenNamed;
		GitIgnoredDisplay m_GitIgnoredDisplay = GitIgnoredDisplay::Dim;
	};
}