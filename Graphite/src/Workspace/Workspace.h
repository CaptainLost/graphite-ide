#pragma once

#include "Core/Base/Base.h"

#include "Settings/WorkspaceSettings.h"

#include "Core/Events/EventBus.h"
#include "Core/FileSystem/DirectoryWatcher.h"
#include "Core/FileSystem/FileTree.h"
#include "Core/Git/Repository.h"
#include "Core/Platform/AsyncDialog.h"
#include "Solution/SolutionReader.h"

#include <cstdint>
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

	// A folder is about to replace whatever was open, published once it is known to open and
	// before it reads anything about itself. What belonged to the folder before - its toasts -
	// goes here rather than on WorkspaceOpenedEvent, which comes after the new folder has already
	// reported its own failures, a solution that would not read among them.
	struct WorkspaceOpeningEvent
	{
		std::filesystem::path Root;
	};

	struct WorkspaceOpenedEvent
	{
		std::filesystem::path Root;
	};

	// The user opened a solution by name (menu or drop), as opposed to picking one among the
	// folder's candidates. The folder is already open by now.
	struct SolutionOpenedEvent
	{
		std::filesystem::path Solution;
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

	// A shell asked for in `Directory`, from the Explorer's menu.
	struct OpenTerminalRequested
	{
		std::filesystem::path Directory;
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

		// Inline for the same reason as RequestOpenFile.
		void RequestOpenTerminal(const std::filesystem::path& directory)
		{
			m_Events.Publish(OpenTerminalRequested{directory});
		}

		bool IsOpen() const { return m_Tree.HasRoot(); }
		const std::filesystem::path& GetRoot() const { return m_Root; }
		Core::FileTree& GetTree() { return m_Tree; }
		const Core::FileTree& GetTree() const { return m_Tree; }
		const std::filesystem::path& GetSelectedFile() const
		{
			return m_SelectedFile;
		}

		const std::optional<Solution>& GetLoadedSolution() const
		{
			return m_Solution;
		}

		// Changes whenever the loaded solution is replaced, reread or dropped, the failed
		// reads among them, so a view can tell that what it waits for has had its chance.
		std::uint64_t GetSolutionLoad() const { return m_SolutionLoads; }
		// Kept even when the Explorer cannot read the solution: a language server may read it all the same.
		const std::filesystem::path& GetSelectedSolution() const { return m_SelectedSolution; }

		const std::vector<std::filesystem::path>& GetSolutionCandidates() const
		{
			return m_SolutionCandidates;
		}

		void SelectSolution(const std::filesystem::path& path);

		WorkspaceSettings& GetFolderSettings() { return m_Settings; }
		const WorkspaceSettings& GetFolderSettings() const { return m_Settings; }

		// The one system dialog the application has open at a time. Here because every dialog
		// there is picks something for the workspace, and every place that opens one already
		// holds it. Polled by Update, so an answer arrives on a later frame.
		Core::AsyncDialog& GetDialog() { return m_Dialog; }
		const Core::AsyncDialog& GetDialog() const { return m_Dialog; }
		bool IsDialogOpen() const { return m_Dialog.IsOpen(); }

		// A dialog that could not be shown, in an application toast: for the callbacks given to
		// GetDialog, which hold the workspace and not the event bus.
		void ReportDialogFailure(const Core::Error& error);

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
		std::uint64_t m_SolutionLoads = 0;
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