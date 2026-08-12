#pragma once

#include "Core/Base/Base.h"

#include "Settings/WorkspaceSettings.h"

#include "Core/Events/EventBus.h"
#include "Core/Fs/DirectoryWatcher.h"
#include "Core/Fs/FileTree.h"
#include "Core/Git/Repository.h"
#include "Sln/SolutionReader.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace Core {

	class CommandRegistry;

}

namespace Graphite {

	struct WorkspaceOpenedEvent
	{
		std::filesystem::path Root;
	};

	struct WorkspaceClosedEvent
	{
	};

	struct FileSelectedEvent
	{
		std::filesystem::path Path;
	};

	struct OpenFileRequested
	{
		std::filesystem::path Path;
		bool Permanent = false;
	};

	struct WorkspaceRootMovedEvent
	{
		std::filesystem::path From;
		std::filesystem::path To;
	};

	struct WorkspaceFilesChangedEvent
	{
		std::vector<std::filesystem::path> Directories;
		bool Everything = false;
	};

	class Workspace final
	{
	public:
		explicit Workspace(Core::EventBus& events) : m_Events(events) {}

		Workspace(Workspace&&) = delete;
		Workspace& operator=(Workspace&&) = delete;

		Core::Result<void> OpenFolder(const std::filesystem::path& folderPath);

		Core::Result<void> OpenSolution(const std::filesystem::path& solutionPath);

		void Close();

		void Update();

		void Reload();

		void SelectFile(const std::filesystem::path& path);

		void RequestOpenFile(const std::filesystem::path& path, bool permanent);

		bool IsOpen() const { return m_Tree.HasRoot(); }
		const std::filesystem::path& GetRoot() const { return m_Root; }
		Core::FileTree& Tree() { return m_Tree; }
		const std::filesystem::path& GetSelectedFile() const
		{
			return m_SelectedFile;
		}

		const std::optional<Solution>& LoadedSolution() const
		{
			return m_Solution;
		}

		const std::vector<std::filesystem::path>& GetSolutionCandidates() const
		{
			return m_SolutionCandidates;
		}

		void SelectSolution(const std::filesystem::path& path);

		WorkspaceSettings& FolderSettings() { return m_Settings; }

		void RegisterCommands(Core::CommandRegistry& commands);

	private:
		Core::FileNode* FindNode(const std::filesystem::path& path);
		void RefreshChanged(const std::filesystem::path& directory);
		void TryOpenRepository();

		bool SyncRepository();

		void HandleDeadWatcher();

		void HandleRootMoved(const std::filesystem::path& destination);

		void FindSolutions();
		void LoadSolution();

		bool TouchesSolution(
			const std::vector<std::filesystem::path>& directories) const;

		std::filesystem::file_time_type ComputeSolutionStamp() const;

		void ForgetSolutionAttempt();
		void ResetSolutionState();

		Core::EventBus& m_Events;
		std::optional<Core::Repository> m_Repository;
		Core::FileTree m_Tree;
		Core::Scope<Core::DirectoryWatcher> m_Watcher;

		bool m_WatcherResumed = false;
		std::filesystem::path m_Root;
		std::filesystem::path m_SelectedFile;

		std::optional<Solution> m_Solution;
		std::vector<std::filesystem::path> m_SolutionCandidates;
		std::filesystem::path m_SelectedSolution;

		std::filesystem::file_time_type m_SolutionStamp;

		std::filesystem::path m_AttemptedSolution;
		bool m_SolutionAttempted = false;

		WorkspaceSettings m_Settings{std::filesystem::path{}};
	};

	void OpenFolderInteractively(Workspace& workspace);

	void OpenSolutionInteractively(Workspace& workspace);
}