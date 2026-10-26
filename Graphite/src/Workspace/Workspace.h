#pragma once

#include "Core/Base/Base.h"

#include "Settings/WorkspaceSettings.h"

#include "Core/Events/EventBus.h"
#include "Core/FileSystem/DirectoryWatcher.h"
#include "Core/FileSystem/FileTree.h"
#include "Core/Git/Repository.h"
#include "Core/Jobs/JobSubmitter.h"
#include "Core/Jobs/Mailbox.h"
#include "Core/Platform/AsyncDialog.h"
#include "Solution/SolutionReader.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace Core {

	class CommandRegistry;

}

namespace Graphite {

	class AppSettings;

	struct WorkspaceOpeningEvent
	{
		std::filesystem::path Root;
	};

	struct WorkspaceOpenedEvent
	{
		std::filesystem::path Root;
	};

	struct SolutionOpenedEvent
	{
		std::filesystem::path Solution;
	};

	struct WorkspaceClosedEvent
	{
	};

	struct OpenFileRequested
	{
		std::filesystem::path Path;
		bool Permanent = false;
	};

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

	struct RepositoryChangedEvent
	{
	};

	using WatcherStart =
		std::function<Core::Result<Core::Scope<Core::DirectoryWatcher>>(const std::filesystem::path& root)>;

	// What a workspace given no worker does with a read: runs it at once, on the calling thread.
	inline void RunJobWhereSubmitted(std::function<void()> job)
	{
		job();
	}

	// A read of the solution as a worker hands it back. The stamp is taken once the projects are named, so the next check
	// of the files on disk compares against all of them.
	struct SolutionRead
	{
		std::uint64_t Generation = 0;
		Core::Result<Solution> Read;
		std::filesystem::file_time_type Stamp;
	};

	class Workspace final
	{
	public:
		Workspace(Core::EventBus& events, AppSettings& settings, WatcherStart startWatcher = Core::DirectoryWatcher::Start,
				  Core::JobSubmitter submitJob = RunJobWhereSubmitted)
			: m_Events(events), m_AppSettings(settings), m_StartWatcher(std::move(startWatcher)),
			  m_SubmitJob(std::move(submitJob))
		{
		}

		Workspace(Workspace&&) = delete;
		Workspace& operator=(Workspace&&) = delete;

		Core::Result<void> OpenFolder(const std::filesystem::path& folderPath);

		Core::Result<void> OpenSolution(const std::filesystem::path& solutionPath);

		void Close();

		void Update();

		// Keeping the ignore answers suits a reload nobody asked for; an explicit refresh asks again, since a rules
		// file outside the watched folder, such as the one core.excludesFile names, changes unseen.
		void Reload(Core::IgnoreAnswers ignoreAnswers);

		// WARNING: defined inline because panels built into the tests do not link Workspace.cpp;
		// all it does is publish.
		void RequestOpenFile(const std::filesystem::path& path, bool permanent)
		{
			m_Events.Publish(OpenFileRequested{path, permanent});
		}

		void RequestOpenTerminal(const std::filesystem::path& directory)
		{
			m_Events.Publish(OpenTerminalRequested{directory});
		}

		bool IsOpen() const { return m_Tree.HasRoot(); }
		const std::filesystem::path& GetRoot() const { return m_Root; }
		Core::FileTree& GetTree() { return m_Tree; }
		const Core::FileTree& GetTree() const { return m_Tree; }

		const std::optional<Solution>& GetLoadedSolution() const
		{
			return m_Solution;
		}

		std::uint64_t GetSolutionLoad() const { return m_SolutionLoads; }

		// While true, GetLoadedSolution holds the solution read before, if it is the same one, or nothing.
		bool IsReadingSolution() const { return m_ReadingSolution; }
		const std::filesystem::path& GetSelectedSolution() const { return m_SelectedSolution; }

		const std::vector<std::filesystem::path>& GetSolutionCandidates() const
		{
			return m_SolutionCandidates;
		}

		void SelectSolution(const std::filesystem::path& path);

		void ReloadSolution();

		WorkspaceSettings& GetFolderSettings() { return m_Settings; }
		const WorkspaceSettings& GetFolderSettings() const { return m_Settings; }

		Core::AsyncDialog& GetDialog() { return m_Dialog; }
		const Core::AsyncDialog& GetDialog() const { return m_Dialog; }
		bool IsDialogOpen() const { return m_Dialog.IsOpen(); }

		void ReportDialogFailure(const Core::Error& error);

		void RegisterCommands(Core::CommandRegistry& commands);

	private:
		Core::FileNode* FindNode(const std::filesystem::path& path);
		void RefreshChanged(const std::filesystem::path& directory);
		void TryOpenRepository();

		void StartWatching(const std::filesystem::path& root, std::string_view failureContext);
		void RereadEverything(Core::IgnoreAnswers ignoreAnswers);
		bool IsInsideRoot(const std::filesystem::path& path) const;
		bool IsIgnoredByRepository(const std::filesystem::path& path) const;
		Core::Result<void> RefuseToOpenSolution(Core::Error error);
		void RememberSolutionCandidate(const std::filesystem::path& path);
		bool HasRepositoryVanished() const;

		bool SyncRepository();

		void HandleDeadWatcher();

		void HandleRootMoved(const std::filesystem::path& destination);

		void FindSolutions();
		void KeepSelectedSolutionListed();
		void LoadSolution();
		void StartReadingSolution();
		void StopReadingSolution();
		void TakeFinishedSolutionReads();
		void ApplySolutionRead(SolutionRead finished);

		bool TouchesSolution(
			const std::vector<std::filesystem::path>& directories) const;

		std::filesystem::file_time_type ComputeSolutionStamp() const;

		void ForgetSolutionAttempt();
		void ResetSolutionState();

		Core::EventBus& m_Events;
		AppSettings& m_AppSettings;
		WatcherStart m_StartWatcher;
		Core::JobSubmitter m_SubmitJob;
		Core::Ref<Core::Mailbox<SolutionRead>> m_FinishedSolutionReads = Core::CreateRef<Core::Mailbox<SolutionRead>>();
		std::uint64_t m_SolutionReadGeneration = 0;
		bool m_ReadingSolution = false;
		std::optional<Core::Repository> m_Repository;
		Core::FileTree m_Tree;
		Core::Scope<Core::DirectoryWatcher> m_Watcher;

		bool m_WatcherResumed = false;
		std::filesystem::path m_Root;

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
