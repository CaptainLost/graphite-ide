#pragma once

#include "Workspace/GitIgnoredDisplay.h"
#include "Workspace/Workspace.h"

#include "Core/Fs/FileTree.h"

#include <imgui.h>

#include <array>
#include <filesystem>
#include <string>

namespace Core {

	class DocumentSet;

}

namespace Graphite {

	class FileTreeView final
	{
	public:
		FileTreeView(Workspace& workspace, const Core::DocumentSet& documents);

		void Render();

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

			Kind kind = Kind::None;
			std::filesystem::path target;
			std::string name; // the new name for Rename, CreateFile and MakeDirectory
		};

		void RenderToolbar();
		void RenderEmptyState();
		void RenderNode(Core::FileNode& node);
		void RenderContextMenu(const Core::FileNode& node);
		void RenderDeleteConfirmation();
		bool RenderRenameEditor(const Core::FileNode& node);
		void RecordAction(PendingAction action);
		void ExecutePendingAction();
		void HandleDelete(const PendingAction& action);
		void HandleRefresh(const PendingAction& action);
		void HandleRename(const PendingAction& action);
		void HandleDuplicate(const PendingAction& action);
		void HandleCreate(const PendingAction& action);
		Core::FileNode* FindNode(const std::filesystem::path& path);
		void RefreshParentOf(const std::filesystem::path& path);
		bool PassesFilter(const Core::FileNode& node) const;

		Workspace& m_Workspace;
		const Core::DocumentSet& m_Documents;
		ImGuiTextFilter m_Filter;
		PendingAction m_Pending;
		std::filesystem::path m_DeleteCandidate;
		bool m_OpenDeleteConfirmation = false;

		std::filesystem::path m_ForceOpenPath;

		std::filesystem::path m_RenamingPath;

		std::array<char, 1024> m_RenameBuffer{};
		bool m_FocusRenameField = false;
		GitIgnoredDisplay m_GitIgnoredDisplay = GitIgnoredDisplay::Dim;
	};
}