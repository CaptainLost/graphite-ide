#pragma once

#include "Explorer/PendingFolderRename.h"
#include "Explorer/RowRenameField.h"
#include "Explorer/TreeReveal.h"
#include "Workspace/Workspace.h"

#include "Core/Base/Result.h"
#include "Git/GitDecorationView.h"
#include "Solution/SolutionModel.h"
#include "UI/Dialogs/ModalQuestion.h"

#include <imgui.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Core {

	class DocumentSet;
	class EventBus;

}

namespace Graphite {

	struct FileRowLayout;

	class SolutionTreeView final
	{
	public:
		SolutionTreeView(Workspace& workspace, const Core::DocumentSet& documents, Core::EventBus& events, ModalGate& modals);

		void Render();

		void SetFollowsActiveFile(bool follow) { m_FollowsActiveFile = follow; }

		void SetGitDecorations(const GitDecorationView& decorations) { m_GitDecorations = decorations; }

		void SetShownFile(const std::filesystem::path& path) { m_ShownFile = path; }

		void WithdrawQuestions();

		bool CanEditSolution() const;

		void NewFolderInRoot();

	private:
		static constexpr const char* kRemoveTitle = "Remove from Solution##solutionTreeRemove";

		struct RemoveQuestion
		{
			std::filesystem::path Project;
			std::string Name;

			bool operator==(const RemoveQuestion&) const = default;
		};

		struct PendingAction
		{
			enum class Kind
			{
				None,
				AddProject,
				RemoveProject,
				AddFolder,
				RemoveFolder,
				RenameFolder,
				MoveProject
			};

			Kind Action = Kind::None;
			std::filesystem::path Project;
			std::string Folder;
			std::string Name;
		};

		void FollowActiveDocument();

		static void ExplainNotEditable(bool editable);

		bool OpensToward(const Folder& folder) const;
		bool OpensToward(const Project& project) const;
		bool OpensToward(const ContentFolder& folder) const;

		void RenderFilter();
		void RenderFolder(const Folder& folder, const std::string& path);
		void RenderChildFolder(const Folder& folder, const std::string& path);
		void RenderProject(const Project& project, const std::string& folder);
		void RenderReferences(const std::vector<std::string>& references);
		void RenderContentFolder(const ContentFolder& folder);
		void RenderContentFile(const ContentFile& file);
		void RenderEmptyState();

		bool IsShown(const Folder& folder);
		bool IsShown(const Project& project);
		bool IsShown(const ContentFolder& folder);
		bool HoldsMatch(const Folder& folder);
		bool HoldsMatch(const Project& project);
		bool HoldsMatch(const ContentFolder& folder);
		bool AnyFilePasses(const ContentFolder& folder) const;
		bool AnyFolderShown(const ContentFolder& folder);
		bool AnyFolderShown(const Folder& folder);
		bool AnyProjectShown(const Folder& folder);
		void ForgetStaleFilterMatches();

		GitDecoration GroupDecorationOf(const Project& project);
		GitDecoration GroupDecorationOf(const Folder& folder);
		void ForgetStaleGroupDecorations();

		void DrawGroupMarker(const Project& project, const FileRowLayout& layout);
		void DrawGroupMarker(const Folder& folder, const FileRowLayout& layout);
		void DrawGroupMarker(GitDecoration decoration, const FileRowLayout& layout);

		void StartRenamingFolder(const Folder& folder, const std::string& path);
		void AddFolderIn(const Folder& folder, const std::string& path);
		void RenderContextMenu(const Folder& folder, const std::string& path);
		void RenderContextMenu(const Project& project, const std::string& folder);
		bool RenderFolderRenameEditor(const Folder& folder,
									  const std::string& path);
		void RenderMoveToFolderMenu(const Project& project,
									const std::string& currentFolder);
		void RenderRemoveConfirmation();
		void ExecutePendingAction();
		Core::Result<void> Apply(const PendingAction& action);
		void ReportEditFailure(const Core::Error& error);
		void ApplyAndReport(const PendingAction& action);

		Workspace& m_Workspace;
		const Core::DocumentSet& m_Documents;
		Core::EventBus& m_Events;
		ModalQuestion<RemoveQuestion> m_RemoveQuestion;
		ImGuiTextFilter m_Filter;

		std::string m_FilterMatchText;
		std::uint64_t m_FilterMatchLoad = 0;
		std::unordered_map<const void*, bool> m_HoldsMatch;

		bool m_ReopenFilteredRows = false;

		GitDecorationView m_GitDecorations;
		std::filesystem::path m_ShownFile;

		std::uint64_t m_GroupLoad = 0;
		const GitDecorations* m_GroupIndex = nullptr;
		std::uint64_t m_GroupVersion = 0;
		std::unordered_map<const void*, GitDecoration> m_GroupDecorations;

		std::filesystem::path m_FollowedPath;
		bool m_FollowsActiveFile = true;
		std::filesystem::path m_RevealPath;

		PendingAction m_Pending;
		RowRenameField m_RenameField;
		std::string m_RenamingFolder;

		PendingFolderRename m_RenameWhenListed;
	};
}