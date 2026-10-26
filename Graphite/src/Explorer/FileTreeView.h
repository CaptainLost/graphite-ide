#pragma once

#include "Workspace/GitIgnoredDisplay.h"
#include "Workspace/Workspace.h"
#include "Explorer/FileOperationJobs.h"
#include "Explorer/FileTemplates.h"
#include "Explorer/FolderListing.h"
#include "Explorer/NewFilePath.h"
#include "Explorer/RowRenameField.h"
#include "Git/GitDecorationView.h"
#include "Theme/ActiveScheme.h"
#include "UI/Dialogs/ModalQuestion.h"

#include "Core/Base/Result.h"
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

		void NewFileInRoot();

		void NewFolderInRoot();

		void Refresh();

		// WARNING: Call every frame, drawn or not, so a copy ending behind a hidden panel still lands.
		void ApplyFinishedOperations();

		std::optional<std::string> FileOperationText() const;

	private:
		static constexpr const char* kDeleteTitle = "Delete##fileTreeDelete";
		static constexpr const char* kNewFileTitle = "New File from Template##fileTreeNewFile";

		struct DeleteQuestion
		{
			std::filesystem::path Target;

			bool operator==(const DeleteQuestion&) const = default;
		};

		struct NewFileQuestion
		{
			std::filesystem::path Folder;

			bool operator==(const NewFileQuestion&) const = default;
		};

		struct FileToCreate
		{
			NewFilePath Path;
			std::string Content;
		};

		struct PendingAction
		{
			enum class Kind
			{
				None,
				Delete,
				Rename,
				BeginNewFile,
				AskNewFileFromTemplate,
				CreateNamedFile,
				MakeDirectory,
				Duplicate,
				Refresh
			};

			Kind Action = Kind::None;
			std::filesystem::path Target;
			std::string Name;
		};

		void FollowActiveDocument();
		bool RenderFilter();
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
		void RenderNewFileEditor(const Core::FileNode& folder);
		void RenderNewFileQuestion();
		void StartRenaming(const Core::FileNode& node);
		void StartRenamingPath(const std::filesystem::path& path);
		void ResolvePendingRename();
		void ForgetRenames();
		bool IsReachable(const std::filesystem::path& path);
		bool IsRowShown(const Core::FileNode& node) const;
		void RecordAction(PendingAction action);
		void BeginNewFileIn(const std::filesystem::path& folder);
		void MakeFolderIn(const std::filesystem::path& folder);
		void ExecutePendingAction();
		void HandleDelete(const PendingAction& action);
		void CloseNameEditorsInside(const std::filesystem::path& folder);
		void HandleRefresh(const PendingAction& action);
		void HandleRename(const PendingAction& action);
		std::filesystem::path ApplyRename(const PendingAction& action);
		void HandleDuplicate(const PendingAction& action);
		void ApplyFinished(const FinishedFileOperation& finished);
		bool IsEditingAName() const;
		bool ShowsBusy(const Core::FileNode& node) const;
		void HandleCreate(const PendingAction& action);
		void HandleBeginNewFile(const PendingAction& action);
		void ContinueNamingNewFile(const std::filesystem::path& folder, std::string_view typed);
		void HandleCreateNamedFile(const PendingAction& action);
		Core::Result<std::filesystem::path> CreateFiles(const std::filesystem::path& folder,
														const std::vector<FileToCreate>& files);
		std::optional<std::string> NewFileProblem(const std::filesystem::path& folder,
												  const std::vector<FileToCreate>& files) const;
		std::vector<FileToCreate> FilesFromTemplate(const std::filesystem::path& folder, FileTemplate fileTemplate,
													std::string_view name) const;
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
		ModalQuestion<NewFileQuestion> m_NewFileQuestion;
		const ActiveScheme& m_Scheme;
		ImGuiTextFilter m_Filter;
		PendingAction m_Pending;
		std::vector<std::filesystem::path> m_ForceOpen;
		std::filesystem::path m_FollowedPath;
		bool m_FollowsActiveFile = true;
		std::filesystem::path m_ScrollToPath;

		std::filesystem::path m_RenamingPath;

		RowRenameField m_RenameField;
		std::filesystem::path m_KnownRoot;
		std::filesystem::path m_PendingRenamePath;

		// The folder whose last row is a field naming a file that does not exist yet; empty when there is none.
		std::filesystem::path m_NewFileIn;
		RowRenameField m_NewFileField;
		std::array<char, 1024> m_NewFileName{};
		int m_NewFileTemplate = 0;
		GitIgnoredDisplay m_GitIgnoredDisplay = GitIgnoredDisplay::Dim;
		GitDecorationView m_GitDecorations;
		std::filesystem::path m_ShownFile;
		FileOperationJobs m_FileOperations;
		FileOperationJobs::Clock::time_point m_FrameTime;
		FolderListing m_Listing;
		Core::FileNode m_FilteredTree;
		std::string m_FilteredTreeText;
		std::uint64_t m_FilteredTreeListingVersion = 0;
		bool m_ReopenFilteredFolders = false;
	};
}