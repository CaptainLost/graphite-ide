#include "Workspace/Workspace.h"

#include "Settings/AppSettings.h"
#include "Solution/SolutionFiles.h"
#include "UI/Dialogs/FileDialogOffer.h"
#include "UI/ToastEvent.h"
#include "Workspace/OwnStateChanges.h"
#include "Workspace/WatcherWarning.h"

#include "Core/Base/Log.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Platform/FileDialog.h"

#include <algorithm>
#include <exception>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	namespace {

		const Core::IgnoreQuery kNoIgnoreQuery;
		const std::filesystem::path kNoFolder;
		constexpr std::string_view kNoFailureContext;
		constexpr std::string_view kSelectedSolutionKey = "selectedSolution";
		constexpr std::string_view kFiltersSuffix = ".filters";
		constexpr std::string_view kGitDirectoryName = ".git";

		bool ContainsPath(const std::vector<std::filesystem::path>& paths,
						  const std::filesystem::path& wanted)
		{
			return std::find(paths.begin(), paths.end(), wanted) != paths.end();
		}

		bool HasProjectInDirectory(const Folder& folder, const std::filesystem::path& directory)
		{
			for (const Project& project : folder.Projects)
			{
				if (project.Path.parent_path() == directory)
				{
					return true;
				}
			}

			for (const Folder& child : folder.Folders)
			{
				if (HasProjectInDirectory(child, directory))
				{
					return true;
				}
			}

			return false;
		}

		void CarryNewestWriteTime(const std::filesystem::path& path,
								  std::filesystem::file_time_type& newest)
		{
			std::error_code fileSystemError;
			const std::filesystem::file_time_type writeTime =
				std::filesystem::last_write_time(path, fileSystemError);

			if (!fileSystemError && writeTime > newest)
			{
				newest = writeTime;
			}
		}

		void CarryNewestProjectWriteTime(const Folder& folder,
										 std::filesystem::file_time_type& newest)
		{
			for (const Project& project : folder.Projects)
			{
				CarryNewestWriteTime(project.Path, newest);

				std::filesystem::path filtersPath = project.Path;
				filtersPath += kFiltersSuffix;

				CarryNewestWriteTime(filtersPath, newest);
			}

			for (const Folder& child : folder.Folders)
			{
				CarryNewestProjectWriteTime(child, newest);
			}
		}

		std::filesystem::file_time_type SolutionStampOf(const std::filesystem::path& solutionPath, const Solution* solution)
		{
			std::error_code fileSystemError;
			std::filesystem::file_time_type newest = std::filesystem::last_write_time(solutionPath, fileSystemError);

			if (fileSystemError)
			{
				newest = std::filesystem::file_time_type{};
			}

			if (solution != nullptr)
			{
				CarryNewestProjectWriteTime(solution->Root, newest);
			}

			return newest;
		}

		SolutionRead ReadSolutionWithStamp(const std::filesystem::path& solutionPath, const SolutionReading& readSolution)
		{
			Core::Result<Solution> read = readSolution(solutionPath);
			const Solution* readSolutionModel = read ? &read.GetValue() : nullptr;
			const std::filesystem::file_time_type stamp = SolutionStampOf(solutionPath, readSolutionModel);

			return SolutionRead{.Read = std::move(read), .StampWithProjectsNamed = stamp};
		}

		IgnoreSourceStamp StampOf(const std::filesystem::path& file)
		{
			IgnoreSourceStamp stamp{.File = file};
			std::error_code queryError;
			const std::filesystem::directory_entry entry{file, queryError};

			if (queryError || !entry.exists(queryError))
			{
				return stamp;
			}

			const std::filesystem::file_time_type writeTime = entry.last_write_time(queryError);

			if (!queryError)
			{
				stamp.WriteTime = writeTime;
			}

			const std::uintmax_t size = entry.file_size(queryError);
			stamp.Size = queryError ? 0 : size;

			return stamp;
		}

		SolutionRead ThrownRead(std::string_view reason)
		{
			return SolutionRead{.Read = Core::Error{.Code = Core::ErrorCode::IoFailure,
													.Message = std::format("the read stopped: {}", reason)},
								.ReaderThrew = true};
		}

		// WARNING: A read that throws posts nothing, which would leave the solution view waiting for good.
		SolutionRead ReadSolutionCatchingThrows(const std::filesystem::path& solutionPath, const SolutionReading& readSolution)
		{
			try
			{
				return ReadSolutionWithStamp(solutionPath, readSolution);
			}
			catch (const std::exception& thrown)
			{
				return ThrownRead(thrown.what());
			}
			catch (...)
			{
				return ThrownRead("unknown failure");
			}
		}
	}

	Core::Result<void> Workspace::OpenFolder(const std::filesystem::path& folderPath)
	{
		const std::filesystem::path root = Core::NormalizeDirectory(folderPath);

		Core::FileTree candidate;

		const Core::Result<void> rooted = candidate.SetRoot(root);

		if (!rooted)
		{
			APP_ERROR("Cannot open folder '{}': {}", Core::ToUtf8(root), rooted.GetError().Message);
			m_Events.Publish(ApplicationToast(Core::ToastKind::Error, "Cannot open folder", rooted.GetError().Message));
			return rooted;
		}

		const Core::Result<void> expanded = candidate.Expand(candidate.GetRoot());

		if (!expanded)
		{
			APP_ERROR("Cannot read folder '{}': {}", Core::ToUtf8(root), expanded.GetError().Message);
			m_Events.Publish(ApplicationToast(
				Core::ToastKind::Error, "Cannot open folder",
				std::format("'{}' cannot be read: {}", Core::ToUtf8(root), expanded.GetError().Message)));
			return expanded;
		}

		m_Events.Publish(WorkspaceOpeningEvent{root});

		m_Tree = std::move(candidate);
		++m_FolderGeneration;
		m_Root = root;

		TryOpenRepository();
		RefreshWholeTree(Core::IgnoreAnswers::AskAgain);

		m_WatcherResumed = false;

		StartWatching(root, kNoFailureContext);

		m_Settings = WorkspaceSettings{root};
		m_SelectedSolution = Core::FromUtf8(m_Settings.Read(kSelectedSolutionKey));

		FindSolutions();
		LoadSolution();

		m_AppSettings.SetLastFolder(root);
		m_Events.Publish(WorkspaceOpenedEvent{root});

		APP_INFO("Workspace opened: '{}'", Core::ToUtf8(root));
		return Core::Result<void>::Ok();
	}

	void Workspace::StartWatching(const std::filesystem::path& root,
								  std::string_view failureContext)
	{
		m_Watcher.reset();

		Core::Result<Core::Scope<Core::DirectoryWatcher>> watcher = m_StartWatcher(root);

		if (!watcher)
		{
			APP_WARN("Cannot watch '{}'{}: {}", Core::ToUtf8(root), failureContext,
					 watcher.GetError().Message);
			m_Events.Publish(NotWatchingWarning(root));
			return;
		}

		m_Watcher = std::move(watcher).GetValue();
	}

	void Workspace::RefreshWholeTree(Core::IgnoreAnswers ignoreAnswers)
	{
		if (ignoreAnswers == Core::IgnoreAnswers::Keep)
		{
			m_Tree.Refresh(m_Tree.GetRoot(), ignoreAnswers);
			return;
		}

		// WARNING: Stamped before asking, so a rule edited while the tree is asked is not taken as seen.
		std::vector<IgnoreSourceStamp> stampsBeforeAsking = StampIgnoreSourcesTheWatcherCannotSee();
		m_Tree.Refresh(m_Tree.GetRoot(), ignoreAnswers);
		m_IgnoreSourceStamps = std::move(stampsBeforeAsking);
	}

	std::vector<IgnoreSourceStamp> Workspace::StampIgnoreSourcesTheWatcherCannotSee() const
	{
		std::vector<IgnoreSourceStamp> stamps;

		if (!m_Repository)
		{
			return stamps;
		}

		for (const std::filesystem::path& file : m_Repository->IgnoreRuleFilesOutside(m_Root))
		{
			stamps.push_back(StampOf(file));
		}

		return stamps;
	}

	void Workspace::RereadEverything(Core::IgnoreAnswers ignoreAnswers)
	{
		RefreshWholeTree(ignoreAnswers);

		FindSolutions();
		LoadSolution();
	}

	bool Workspace::IsInsideRoot(const std::filesystem::path& path) const
	{
		if (!IsOpen())
		{
			return false;
		}

		return Core::IsSameOrWithin(path, m_Root);
	}

	void Workspace::RememberSolutionCandidate(const std::filesystem::path& path)
	{
		if (ContainsPath(m_SolutionCandidates, path))
		{
			return;
		}

		m_SolutionCandidates.push_back(path);
		std::sort(m_SolutionCandidates.begin(), m_SolutionCandidates.end(), ListsSolutionBefore);
	}

	void Workspace::RequestOpenFolder(const std::filesystem::path& folderPath)
	{
		const auto open = [this, folderPath] {
			// OpenFolder reports its own failure and leaves the current folder open.
			static_cast<void>(OpenFolder(folderPath));
		};

		if (!m_ReplaceGate)
		{
			open();
			return;
		}

		m_ReplaceGate(open);
	}

	void Workspace::RequestOpenSolution(const std::filesystem::path& solutionPath)
	{
		const auto open = [this, solutionPath] {
			// Logged only: OpenSolution reports every failure in a toast itself.
			if (const Core::Result<void> opened = OpenSolution(solutionPath); !opened)
			{
				APP_ERROR("Cannot open the solution: {}", opened.GetError().Message);
			}
		};

		// A solution inside the open folder is only selected, and closes nothing.
		std::error_code absoluteError;
		const std::filesystem::path absolute = std::filesystem::absolute(solutionPath, absoluteError);

		if (!m_ReplaceGate || (!absoluteError && IsInsideRoot(absolute.lexically_normal())))
		{
			open();
			return;
		}

		m_ReplaceGate(open);
	}

	Core::Result<void> Workspace::RefuseToOpenSolution(Core::Error error)
	{
		m_Events.Publish(ApplicationToast(Core::ToastKind::Error, "Cannot open solution", error.Message));
		return error;
	}

	Core::Result<void> Workspace::OpenSolution(const std::filesystem::path& solutionPath)
	{
		std::error_code fileSystemError;

		if (!std::filesystem::is_regular_file(solutionPath, fileSystemError))
		{
			return RefuseToOpenSolution(
				Core::Error{Core::ErrorCode::NotFound,
							std::format("no solution file at '{}'", Core::ToUtf8(solutionPath))});
		}

		if (!IsSolutionFile(solutionPath))
		{
			return RefuseToOpenSolution(
				Core::Error{Core::ErrorCode::Unsupported,
							std::format("'{}' is not a solution file", Core::ToUtf8(solutionPath))});
		}

		std::filesystem::path chosen = std::filesystem::absolute(solutionPath, fileSystemError);

		if (fileSystemError)
		{
			chosen = solutionPath;
		}

		chosen = chosen.lexically_normal();
		chosen.make_preferred();

		if (!IsInsideRoot(chosen))
		{
			const Core::Result<void> opened = OpenFolder(chosen.parent_path());

			if (!opened)
			{
				return opened;
			}
		}
		else
		{
			ForgetSolutionAttempt();
		}

		RememberSolutionCandidate(chosen);
		SelectSolution(chosen);

		m_Events.Publish(SolutionOpenedEvent{chosen});

		const bool readFailed = !m_Solution && !m_SolutionRead.IsInFlight();

		if (readFailed)
		{
			return Core::Error{Core::ErrorCode::InvalidArgument,
							   std::format("cannot read '{}'", Core::ToUtf8(chosen))};
		}

		return Core::Result<void>::Ok();
	}

	void Workspace::Close()
	{
		if (!IsOpen())
		{
			return;
		}

		const std::filesystem::path closed = m_Root;

		// WARNING: the watcher thread reports into the tree replaced below, so it stops first.
		m_Watcher.reset();

		// WARNING: the ignore query reads the repository, so it goes first; between the two
		// statements the tree would otherwise hold a query pointing at an empty optional.
		m_Tree.SetIgnoreQuery(kNoIgnoreQuery);
		m_Repository.reset();
		m_Tree = Core::FileTree{};
		++m_FolderGeneration;
		m_Root.clear();

		ResetSolutionState();

		m_Settings = WorkspaceSettings{kNoFolder};

		m_AppSettings.SetLastFolder(kNoFolder);
		m_Events.Publish(WorkspaceClosedEvent{});

		APP_INFO("Workspace closed: '{}'", Core::ToUtf8(closed));
	}

	void Workspace::Update()
	{
		m_Dialog.Poll();
		TakeFinishedSolutionReads();

		if (!m_Watcher)
		{
			return;
		}

		if (!m_Watcher->IsRunning())
		{
			HandleDeadWatcher();
			return;
		}

		const Core::DirectoryWatcher::Changes changes = m_Watcher->TakeSettled();

		if (changes.MovedTo)
		{
			HandleRootMoved(*changes.MovedTo);
			return;
		}

		if (changes.RepositoryStateChanged)
		{
			m_Events.Publish(RepositoryChangedEvent{});
		}

		if (changes.Directories.empty() && !changes.Overflowed && !changes.IgnoreRulesChanged)
		{
			return;
		}

		// WARNING: the repository is settled before any directory is refreshed, because
		// installing or dropping the ignore query changes what every node says.
		const bool repositoryChanged = SyncRepository();

		if (repositoryChanged || changes.Overflowed)
		{
			if (changes.Overflowed)
			{
				APP_TRACE("Filesystem changes were dropped, refreshing the whole tree");
			}

			RereadEverything(Core::IgnoreAnswers::AskAgain);
			m_Events.Publish(WorkspaceFilesChangedEvent{.Everything = true});
			return;
		}

		if (changes.IgnoreRulesChanged)
		{
			APP_TRACE("An ignore rules file changed, asking again about every entry of the tree");
			RefreshWholeTree(Core::IgnoreAnswers::AskAgain);
		}
		else
		{
			for (const std::filesystem::path& directory : changes.Directories)
			{
				RefreshChanged(directory);
			}
		}

		if (changes.Directories.empty())
		{
			return;
		}

		if (TouchesSolution(changes.Directories))
		{
			LoadSolution();
		}

		if (OnlyOwnStateChanged(changes.Directories, m_Root))
		{
			return;
		}

		m_Events.Publish(WorkspaceFilesChangedEvent{changes.Directories, false});
	}

	bool Workspace::TouchesSolution(const std::vector<std::filesystem::path>& directories) const
	{
		if (m_SelectedSolution.empty())
		{
			return false;
		}

		const std::filesystem::path solutionDirectory = m_SelectedSolution.parent_path();

		for (const std::filesystem::path& directory : directories)
		{
			if (directory == solutionDirectory)
			{
				return true;
			}

			if (!m_Solution)
			{
				continue;
			}

			if (HasProjectInDirectory(m_Solution->Root, directory))
			{
				return true;
			}
		}

		return false;
	}

	void Workspace::Reload(Core::IgnoreAnswers ignoreAnswers)
	{
		if (!m_Tree.HasRoot())
		{
			return;
		}

		const bool repositoryChanged = SyncRepository();
		const bool unseenRulesChanged = StampIgnoreSourcesTheWatcherCannotSee() != m_IgnoreSourceStamps;
		const bool asksAgain = repositoryChanged || unseenRulesChanged;

		RereadEverything(asksAgain ? Core::IgnoreAnswers::AskAgain : ignoreAnswers);
	}

	bool Workspace::HasRepositoryVanished() const
	{
		const std::filesystem::path workingDirectory = m_Repository->GetWorkingDirectory();

		if (workingDirectory.empty())
		{
			return false;
		}

		std::error_code fileSystemError;
		const bool gitDirectoryPresent =
			std::filesystem::exists(workingDirectory / kGitDirectoryName, fileSystemError);

		return !fileSystemError && !gitDirectoryPresent;
	}

	bool Workspace::SyncRepository()
	{
		if (!m_Repository)
		{
			TryOpenRepository();
			return m_Repository.has_value();
		}

		if (!HasRepositoryVanished())
		{
			return false;
		}

		// WARNING: libgit2 does not notice a deleted .git; it keeps answering from the handle it
		// holds and reads .gitignore straight out of the working tree.
		APP_INFO("The git repository at '{}' is gone",
				 Core::ToUtf8(m_Repository->GetWorkingDirectory()));

		m_Tree.SetIgnoreQuery(kNoIgnoreQuery);
		m_Repository.reset();

		return true;
	}

	void Workspace::HandleDeadWatcher()
	{
		m_Watcher.reset();

		if (m_WatcherResumed)
		{
			APP_WARN("Giving up watching '{}' for changes: it failed twice",
					 Core::ToUtf8(m_Root));
			m_Events.Publish(NotWatchingWarning(m_Root));
			return;
		}

		m_WatcherResumed = true;

		Core::Result<Core::Scope<Core::DirectoryWatcher>> watcher = m_StartWatcher(m_Root);

		if (!watcher)
		{
			APP_WARN("Cannot resume watching '{}': {}", Core::ToUtf8(m_Root),
					 watcher.GetError().Message);
			m_Events.Publish(NotWatchingWarning(m_Root));
			return;
		}

		m_Watcher = std::move(watcher).GetValue();

		SyncRepository();

		// WARNING: Nothing watched the folder while the watcher was down, so a rules file may have changed unseen.
		RefreshWholeTree(Core::IgnoreAnswers::AskAgain);

		APP_INFO("Resumed watching '{}' after a failure", Core::ToUtf8(m_Root));
	}

	void Workspace::HandleRootMoved(const std::filesystem::path& destination)
	{
		const std::filesystem::path previousRoot = m_Root;
		const std::filesystem::path movedRoot = Core::NormalizeDirectory(destination);

		APP_INFO("Workspace moved from '{}' to '{}'", Core::ToUtf8(previousRoot), Core::ToUtf8(movedRoot));

		m_Root = movedRoot;
		++m_FolderGeneration;
		m_Tree.Rebase(movedRoot);

		// WARNING: the watcher followed the directory, but the copy of the root inside its thread
		// is stale and every reported path is built from it, so it is started again.
		m_WatcherResumed = false;

		StartWatching(movedRoot, " after the move");

		// WARNING: libgit2 bakes the path into the handle, so the repository is opened again.
		TryOpenRepository();

		m_Settings = WorkspaceSettings{movedRoot};
		ResetSolutionState();
		m_SelectedSolution = Core::FromUtf8(m_Settings.Read(kSelectedSolutionKey));

		RereadEverything(Core::IgnoreAnswers::AskAgain);

		m_AppSettings.SetLastFolder(movedRoot);
		m_Events.Publish(WorkspaceRootMovedEvent{previousRoot, movedRoot});
	}

	void Workspace::RefreshChanged(const std::filesystem::path& directory)
	{
		Core::FileNode* node = FindNode(directory);

		if (node != nullptr && node->IsDirectory && node->ChildrenLoaded)
		{
			m_Tree.Refresh(*node);
		}
	}

	// WARNING: Refresh rebuilds a node's children vector, so a FileNode* kept across a Refresh
	// dangles; callers look the node up again each time.
	Core::FileNode* Workspace::FindNode(const std::filesystem::path& path)
	{
		if (!m_Tree.HasRoot())
		{
			return nullptr;
		}

		std::vector<Core::FileNode*> pending{&m_Tree.GetRoot()};

		while (!pending.empty())
		{
			Core::FileNode* node = pending.back();
			pending.pop_back();

			if (node->Path == path)
			{
				return node;
			}

			for (Core::FileNode& child : node->Children)
			{
				if (child.IsDirectory)
				{
					pending.push_back(&child);
				}
			}
		}

		return nullptr;
	}

	bool Workspace::IsIgnoredByRepository(const std::filesystem::path& path) const
	{
		return m_Repository && m_Repository->IsIgnored(path);
	}

	void Workspace::TryOpenRepository()
	{
		m_Tree.SetIgnoreQuery(kNoIgnoreQuery);
		m_Repository.reset();

		Core::Result<Core::Repository> repository = Core::Repository::Open(m_Root);

		if (!repository)
		{
			APP_TRACE("'{}' is not inside a git repository: {}", Core::ToUtf8(m_Root),
					  repository.GetError().Message);
			return;
		}

		m_Repository.emplace(std::move(repository).GetValue());

		m_Tree.SetIgnoreQuery([this](const std::filesystem::path& path, [[maybe_unused]] bool isDirectory) {
			return IsIgnoredByRepository(path);
		});

		APP_INFO("Git repository detected at '{}'", Core::ToUtf8(m_Repository->GetWorkingDirectory()));
	}

	void Workspace::FindSolutions()
	{
		m_SolutionCandidates = Graphite::FindSolutions(m_Root);

		KeepSelectedSolutionListed();

		std::sort(m_SolutionCandidates.begin(), m_SolutionCandidates.end(), ListsSolutionBefore);
	}

	void Workspace::KeepSelectedSolutionListed()
	{
		if (m_SelectedSolution.empty())
		{
			return;
		}

		if (ContainsPath(m_SolutionCandidates, m_SelectedSolution))
		{
			return;
		}

		std::error_code fileSystemError;

		if (std::filesystem::is_regular_file(m_SelectedSolution, fileSystemError))
		{
			m_SolutionCandidates.push_back(m_SelectedSolution);
		}
	}

	void Workspace::LoadSolution()
	{
		if (m_SolutionCandidates.empty())
		{
			if (m_Solution)
			{
				++m_SolutionLoads;
			}

			m_Solution.reset();
			m_SolutionReadFailure.reset();
			m_SelectedSolution.clear();
			ForgetSolutionAttempt();
			StopReadingSolution();
			return;
		}

		const bool remembered = ContainsPath(m_SolutionCandidates, m_SelectedSolution);

		if (!remembered)
		{
			m_SelectedSolution = m_SolutionCandidates.front();
		}

		const std::filesystem::file_time_type stamp = ComputeSolutionStamp();

		if (m_SolutionAttempted && m_AttemptedSolution == m_SelectedSolution &&
			stamp == m_SolutionStamp)
		{
			return;
		}

		m_SolutionAttempted = true;
		m_AttemptedSolution = m_SelectedSolution;
		m_SolutionStamp = stamp;

		const bool rereadsTheShownSolution = m_Solution.has_value() && m_Solution->Path == m_SelectedSolution;

		if (!rereadsTheShownSolution)
		{
			m_Solution.reset();
			++m_SolutionLoads;
		}

		m_ReadingAgainAfterAThrow = false;
		StartReadingSolution();
	}

	void Workspace::StartReadingSolution()
	{
		m_SolutionRead.Ask([solutionPath = m_SelectedSolution, readSolution = m_ReadSolution](const Core::CancellationToken&) {
			return ReadSolutionCatchingThrows(solutionPath, readSolution);
		});

		// WARNING: A job that runs where it is submitted has already finished here.
		TakeFinishedSolutionReads();
	}

	// WARNING: A read already running cannot be stopped, so its answer is dropped when it lands instead.
	void Workspace::StopReadingSolution()
	{
		m_SolutionRead.Forget();
		m_ReadingAgainAfterAThrow = false;
	}

	void Workspace::TakeFinishedSolutionReads()
	{
		if (std::optional<SolutionRead> finished = m_SolutionRead.TakeNewest())
		{
			ApplySolutionRead(std::move(*finished));
		}
	}

	void Workspace::ApplySolutionRead(SolutionRead finished)
	{
		const bool firstThrowInARow = !finished.Read && finished.ReaderThrew && !m_ReadingAgainAfterAThrow;

		// WARNING: A throw says nothing about the files, so it is read once more at once; a second throw waits for them.
		if (firstThrowInARow)
		{
			APP_WARN("Reading '{}' stopped ({}); reading it once more", Core::ToUtf8(m_SelectedSolution),
					 finished.Read.GetError().Message);
			m_ReadingAgainAfterAThrow = true;
			StartReadingSolution();
			return;
		}

		m_ReadingAgainAfterAThrow = false;
		++m_SolutionLoads;

		if (!finished.Read)
		{
			m_Solution.reset();
			m_SolutionReadFailure = finished.Read.GetError().Message;

			APP_WARN("Cannot read '{}': {}", Core::ToUtf8(m_SelectedSolution), finished.Read.GetError().Message);

			m_Events.Publish(FolderToast(Core::ToastKind::Error, "Cannot open solution",
										 std::format("Cannot read '{}': {}", Core::ToUtf8(m_SelectedSolution),
													 finished.Read.GetError().Message),
										 m_Root));
			return;
		}

		m_Solution.emplace(std::move(finished.Read).GetValue());
		m_SolutionReadFailure.reset();

		// WARNING: The stamp taken at the start saw the solution file alone, and would make the next check read again.
		m_SolutionStamp = finished.StampWithProjectsNamed;

		APP_INFO("Solution '{}' loaded with {} projects", m_Solution->Name, m_Solution->ProjectCount);
	}

	std::filesystem::file_time_type Workspace::ComputeSolutionStamp() const
	{
		const Solution* shownSolution = m_Solution.has_value() ? &*m_Solution : nullptr;

		return SolutionStampOf(m_SelectedSolution, shownSolution);
	}

	void Workspace::ForgetSolutionAttempt()
	{
		m_SolutionAttempted = false;
		m_AttemptedSolution.clear();
		m_SolutionStamp = std::filesystem::file_time_type{};
	}

	void Workspace::ResetSolutionState()
	{
		m_Solution.reset();
		m_SolutionReadFailure.reset();
		++m_SolutionLoads;
		m_SolutionCandidates.clear();
		m_SelectedSolution.clear();
		ForgetSolutionAttempt();
		StopReadingSolution();
	}

	void Workspace::SelectSolution(const std::filesystem::path& path)
	{
		m_SelectedSolution = path;
		m_Settings.Write(kSelectedSolutionKey, Core::ToUtf8(path));

		LoadSolution();
	}

	// WARNING: The attempt is forgotten so the solution is read even when its write time did not move on, as a save
	// within the file system's timestamp step leaves it.
	void Workspace::ReloadSolution()
	{
		if (!IsOpen())
		{
			return;
		}

		ForgetSolutionAttempt();
		LoadSolution();
	}

	void Workspace::RegisterCommands(Core::CommandRegistry& commands)
	{
		commands.Add({.Id = "workspace.openFolder",
					  .Label = "Open Folder...",
					  .Run = [this] { OpenFolderInteractively(*this); },
					  .Enabled = [this] { return !IsDialogOpen(); },
					  .DisabledNote = kFileDialogOpenNote,
					  .RunWhileDisabled = [this] { m_Dialog.BringToFront(); }});

		commands.Add({.Id = "workspace.openSolution",
					  .Label = "Open Solution...",
					  .Run = [this] { OpenSolutionInteractively(*this); },
					  .Enabled = [this] { return !IsDialogOpen(); },
					  .DisabledNote = kFileDialogOpenNote,
					  .RunWhileDisabled = [this] { m_Dialog.BringToFront(); }});
	}

	void Workspace::ReportDialogFailure(const Core::Error& error)
	{
		APP_ERROR("Cannot open dialog: {}", error.Message);
		m_Events.Publish(ApplicationToast(Core::ToastKind::Error, "Cannot open dialog", error.Message));
	}

	// WARNING: the dialog answers on a later frame from Workspace::Update, on a thread of its
	// own; the callbacks hold the workspace by reference because the workspace owns the dialog.
	void OpenFolderInteractively(Workspace& workspace)
	{
		if (workspace.IsDialogOpen())
		{
			workspace.GetDialog().BringToFront();
			return;
		}

		workspace.GetDialog().Start(
			[] { return Core::FileDialog::PickFolder("Open Folder"); },
			[&workspace](const std::filesystem::path& picked) { workspace.RequestOpenFolder(picked); },
			[&workspace](const Core::Error& error) { workspace.ReportDialogFailure(error); });
	}

	void OpenSolutionInteractively(Workspace& workspace)
	{
		if (workspace.IsDialogOpen())
		{
			workspace.GetDialog().BringToFront();
			return;
		}

		const std::vector<Core::FileDialog::Filter> filters = {
			{"Visual Studio solutions", "*.sln;*.slnx"},
			{"All files", "*.*"}};

		workspace.GetDialog().Start(
			[filters] { return Core::FileDialog::PickFile("Open Solution", filters); },
			[&workspace](const std::filesystem::path& picked) { workspace.RequestOpenSolution(picked); },
			[&workspace](const Core::Error& error) { workspace.ReportDialogFailure(error); });
	}
}
