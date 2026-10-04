#pragma once

#include "Explorer/PendingFolderRename.h"
#include "Explorer/TreeReveal.h"
#include "Workspace/Workspace.h"

#include "Core/Base/Result.h"
#include "Git/GitDecorationView.h"
#include "Solution/SolutionModel.h"

#include <imgui.h>

#include <array>
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

	class SolutionTreeView final
	{
	public:
		SolutionTreeView(Workspace& workspace, const Core::DocumentSet& documents, Core::EventBus& events);

		void Render();

		// Off, the tree is left as the user arranged it; turned back on, it reveals the
		// active document straight away.
		void SetFollowsActiveFile(bool follow) { m_FollowsActiveFile = follow; }

		// This frame's git status, set before the tree renders.
		void SetGitDecorations(const GitDecorationView& decorations) { m_GitDecorations = decorations; }

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

			Kind Action = Kind::None;
			std::filesystem::path Project;
			std::string Folder;
			std::string Name;
		};

		void FollowActiveDocument();

		// The editor cannot write every solution format; its menu items are disabled, with
		// the reason on hover, rather than failing when used.
		bool CanEditSolution() const;
		static void ExplainNotEditable(bool editable);

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

		// The most important decoration among the content files a project or solution folder
		// holds. Computed the first time a row asks and kept until the solution is reloaded or the
		// status index rebuilt, so a collapsed project's files are walked once, not every frame.
		GitDecoration GroupDecorationOf(const Project& project);
		GitDecoration GroupDecorationOf(const Folder& folder);
		void ForgetStaleGroupDecorations();

		// A dot on the right of the row just submitted, in the colour of its group's decoration.
		// Drawn only for a row on screen.
		void DrawGroupMarker(const Project& project);
		void DrawGroupMarker(const Folder& folder);
		void DrawGroupMarker(GitDecoration decoration);

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
		Core::EventBus& m_Events;
		ImGuiTextFilter m_Filter;

		GitDecorationView m_GitDecorations;

		// What m_GroupDecorations were computed from: the load of the solution, and the status
		// index with its version (the pointer alone can repeat across rebuilds).
		std::uint64_t m_GroupLoad = 0;
		const GitDecorations* m_GroupIndex = nullptr;
		std::uint64_t m_GroupVersion = 0;
		std::unordered_map<const void*, GitDecoration> m_GroupDecorations;

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

		// A folder just written to the solution file, which the reload has yet to list; its
		// rename starts on the frame the walk reaches it.
		PendingFolderRename m_RenameWhenListed;
	};
}