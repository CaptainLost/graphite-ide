#pragma once

#include "Core/Base/Base.h"

#include "Settings/WorkspaceSettings.h"

#include "Core/Events/EventBus.h"
#include "Core/Fs/DirectoryWatcher.h"
#include "Core/Fs/FileTree.h"
#include "Core/Git/Repository.h"
#include "Core/Platform/AsyncDialog.h"
#include "Sln/SolutionReader.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace Core {

	class CommandRegistry;

}

namespace Graphite {

	class AppSettings;

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

	// The repository's own state changed under the application - a commit, a checkout, a
	// fetch or a rebase run in a terminal beside it. Carried apart from
	// WorkspaceFilesChangedEvent because the working tree did not change: nothing in the
	// file tree has anything to redraw, and everything that draws git data is stale.
	//
	// Empty on purpose. What moved is a question every subscriber answers by asking git
	// again, and naming the file that moved would invite a subscriber to guess instead.
	struct RepositoryChangedEvent
	{
	};

	class Workspace final
	{
	public:
		Workspace(Core::EventBus& events, AppSettings& settings)
			: m_Events(events), m_AppSettings(settings)
		{
		}

		Workspace(Workspace&&) = delete;
		Workspace& operator=(Workspace&&) = delete;

		Core::Result<void> OpenFolder(const std::filesystem::path& folderPath);

		Core::Result<void> OpenSolution(const std::filesystem::path& solutionPath);

		void Close();

		void Update();

		void Reload();

		void SelectFile(const std::filesystem::path& path);

		// Inline so a panel built into the tests, which do not link Workspace.cpp, can call it:
		// all it does is publish.
		void RequestOpenFile(const std::filesystem::path& path, bool permanent)
		{
			m_Events.Publish(OpenFileRequested{path, permanent});
		}

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

		// The one system dialog the application has open at a time. Here because every dialog
		// there is picks something for the workspace, and every place that opens one already
		// holds it. Polled by Update, so an answer arrives on a later frame.
		Core::AsyncDialog& Dialog() { return m_Dialog; }
		bool IsDialogOpen() const { return m_Dialog.IsOpen(); }

		void RegisterCommands(Core::CommandRegistry& commands);

	private:
		Core::FileNode* FindNode(const std::filesystem::path& path);
		void RefreshChanged(const std::filesystem::path& directory);
		void TryOpenRepository();

		void StartWatching(const std::filesystem::path& root, std::string_view failureContext);
		void RereadEverything();
		bool IsInsideRoot(const std::filesystem::path& path) const;
		void RememberSolutionCandidate(const std::filesystem::path& path);
		bool HasRepositoryVanished() const;

		bool SyncRepository();

		void HandleDeadWatcher();

		void HandleRootMoved(const std::filesystem::path& destination);

		void FindSolutions();
		void KeepSelectedSolutionListed();
		void LoadSolution();

		bool TouchesSolution(
			const std::vector<std::filesystem::path>& directories) const;

		std::filesystem::file_time_type ComputeSolutionStamp() const;

		void ForgetSolutionAttempt();
		void ResetSolutionState();

		Core::EventBus& m_Events;
		AppSettings& m_AppSettings;
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

		Core::AsyncDialog m_Dialog;
	};

	void OpenFolderInteractively(Workspace& workspace);

	void OpenSolutionInteractively(Workspace& workspace);
}