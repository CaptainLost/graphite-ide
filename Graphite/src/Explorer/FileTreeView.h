#pragma once

#include "Workspace/GitIgnoredDisplay.h"
#include "Workspace/Workspace.h"
#include "Explorer/FolderListing.h"
#include "Explorer/RenameSelection.h"
#include "Git/GitDecorationView.h"

#include "Core/FileSystem/FileTree.h"
#include "Core/ImGui/ToastStack.h"
#include "Core/Jobs/JobSubmitter.h"

#include <imgui.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

	class DocumentSet;
	class EventBus;

}

namespace Graphite {

	class FileTreeView final
	{
	public:
		FileTreeView(Workspace& workspace, const Core::DocumentSet& documents, Core::EventBus& events,
					 Core::JobSubmitter submitJob);

		void Render();

		// Off, the tree is left as the user arranged it; turned back on, it reveals the
		// active document straight away.
		void SetFollowsActiveFile(bool follow) { m_FollowsActiveFile = follow; }

		void SetGitIgnoredDisplay(GitIgnoredDisplay display) { m_GitIgnoredDisplay = display; }

		// This frame's git status, set before Render. A view with no index decorates nothing.
		void SetGitDecorations(const GitDecorationView& decorations) { m_GitDecorations = decorations; }

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
		void RenderFilteredTree();
		void RenderSearching();
		void RebuildFilteredTreeWhenOutOfDate();
		void NoteFilesChanged(const WorkspaceFilesChangedEvent& changed);
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
		void ForgetRenames();
		bool IsReachable(const std::filesystem::path& path);
		bool IsRowShown(const Core::FileNode& node) const;
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

		// A failed file action in a toast, beside the log line each site keeps.
		void ReportFailure(Core::ToastKind kind, std::string title, std::string_view message);

		// A name the file system refuses, in a toast; an empty one is a cancel and raises none.
		void ReportInvalidName(std::string title, const std::string& name);

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

		// The workspace folder the tree last drew, to notice a different one.
		std::filesystem::path m_KnownRoot;

		// A node just created or duplicated that the tree did not yet hold when its rename
		// was asked for; the rename starts on the first frame the node can be found.
		std::filesystem::path m_PendingRenamePath;

		// A file just created, opened in a permanent tab once its inline rename ends under
		// whatever name it was given.
		std::filesystem::path m_OpenWhenNamed;
		GitIgnoredDisplay m_GitIgnoredDisplay = GitIgnoredDisplay::Dim;
		GitDecorationView m_GitDecorations;

		// What the tree shows while the filter has text, built from the listing rather than from
		// the tree, which holds only the folders the user has opened.
		FolderListing m_Listing;
		Core::FileNode m_FilteredTree;
		std::string m_FilteredTreeText;
		std::uint64_t m_FilteredTreeListingVersion = 0;
		bool m_ReopenFilteredFolders = false;

		// When, in ImGui's clock, the filter started waiting for the first listing of the folder.
		std::optional<double> m_SearchingSince;
	};
}