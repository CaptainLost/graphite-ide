#pragma once

#include "Workspace/GitIgnoredDisplay.h"
#include "Workspace/Workspace.h"
#include "Explorer/FolderListing.h"
#include "Explorer/RenameSelection.h"
#include "Git/GitDecorationView.h"
#include "Theme/ActiveScheme.h"
#include "UI/ModalQuestion.h"

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
		FileTreeView(Workspace& workspace, const Core::DocumentSet& documents, Core::EventBus& events, ModalGate& modals,
					 const ActiveScheme& scheme, Core::JobSubmitter submitJob);

		void Render();

		void SetFollowsActiveFile(bool follow) { m_FollowsActiveFile = follow; }

		void SetGitIgnoredDisplay(GitIgnoredDisplay display) { m_GitIgnoredDisplay = display; }

		void SetGitDecorations(const GitDecorationView& decorations) { m_GitDecorations = decorations; }

		void SetShownFile(const std::filesystem::path& path) { m_ShownFile = path; }

		void WithdrawQuestions();

	private:
		static constexpr const char* kDeleteTitle = "Delete##fileTreeDelete";

		struct DeleteQuestion
		{
			std::filesystem::path Target;

			bool operator==(const DeleteQuestion&) const = default;
		};

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

		void ReportFailure(Core::ToastKind kind, std::string title, std::string_view message);

		void ReportInvalidName(std::string title, const std::string& name);

		Workspace& m_Workspace;
		const Core::DocumentSet& m_Documents;
		Core::EventBus& m_Events;
		ModalQuestion<DeleteQuestion> m_DeleteQuestion;
		const ActiveScheme& m_Scheme;
		ImGuiTextFilter m_Filter;
		PendingAction m_Pending;
		std::vector<std::filesystem::path> m_ForceOpen;
		std::filesystem::path m_FollowedPath;
		bool m_FollowsActiveFile = true;
		std::filesystem::path m_ScrollToPath;

		std::filesystem::path m_RenamingPath;

		std::array<char, 1024> m_RenameBuffer{};
		bool m_FocusRenameField = false;
		RenameSpan m_RenameSpan;
		std::filesystem::path m_KnownRoot;
		std::filesystem::path m_PendingRenamePath;
		std::filesystem::path m_OpenWhenNamed;
		GitIgnoredDisplay m_GitIgnoredDisplay = GitIgnoredDisplay::Dim;
		GitDecorationView m_GitDecorations;
		std::filesystem::path m_ShownFile;
		FolderListing m_Listing;
		Core::FileNode m_FilteredTree;
		std::string m_FilteredTreeText;
		std::uint64_t m_FilteredTreeListingVersion = 0;
		bool m_ReopenFilteredFolders = false;
		std::optional<double> m_SearchingSince;
	};
}