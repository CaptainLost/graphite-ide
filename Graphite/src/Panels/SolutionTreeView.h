#pragma once

#include "Panels/TreeReveal.h"
#include "Workspace/Workspace.h"

#include "Core/Base/Result.h"
#include "Sln/SolutionModel.h"

#include <imgui.h>

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace Core {

	class DocumentSet;

}

namespace Graphite {

	class SolutionTreeView final
	{
	public:
		SolutionTreeView(Workspace& workspace, const Core::DocumentSet& documents);

		void Render();

		// Off, the tree is left as the user arranged it; turned back on, it reveals the
		// active document straight away.
		void SetFollowsActiveFile(bool follow) { m_FollowsActiveFile = follow; }

	private:
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

			Kind kind = Kind::None;
			std::filesystem::path project;
			std::string folder;
			std::string name;
		};

		void FollowActiveDocument();

		// Whether the walk opens `node` because the file being revealed lies under it. The
		// search runs only on the frame of a reveal.
		template<typename Node>
		bool OpensToward(const Node& node) const
		{
			return !m_RevealPath.empty() && HoldsFile(node, m_RevealPath);
		}

		void RenderFilter();
		void RenderFolder(const Folder& folder, const std::string& path);
		void RenderProject(const Project& project, const std::string& folder);
		void RenderReferences(const std::vector<std::string>& references);
		void RenderContentFolder(const ContentFolder& folder);
		void RenderContentFile(const ContentFile& file);
		void RenderEmptyState();

		void StartRenamingFolder(const Folder& folder, const std::string& path);
		void RenderContextMenu(const Folder& folder, const std::string& path);
		void RenderContextMenu(const Project& project, const std::string& folder);
		bool RenderFolderRenameEditor(const Folder& folder,
									  const std::string& path);
		void RenderMoveToFolderMenu(const Project& project,
									const std::string& currentFolder);
		void RenderRemoveConfirmation();
		void ExecutePendingAction();
		Core::Result<void> Apply(const PendingAction& action);
		void ApplyAndReport(const PendingAction& action);

		Workspace& m_Workspace;
		const Core::DocumentSet& m_Documents;
		ImGuiTextFilter m_Filter;

		// The active document the tree last revealed, and the file the next walk opens every
		// row on the way to and scrolls into view. Spent by that one walk, so a row the user
		// closes afterwards stays closed.
		std::filesystem::path m_FollowedPath;
		bool m_FollowsActiveFile = true;
		std::filesystem::path m_RevealPath;

		PendingAction m_Pending;
		std::filesystem::path m_RemoveCandidate;
		std::string m_RemoveCandidateName;
		bool m_OpenRemoveConfirmation = false;
		std::array<char, 256> m_RenameBuffer{};
		std::string m_RenamingFolder;
		bool m_FocusRenameField = false;
	};
}