#include "Workspace/Workspace.h"

#include "Settings/AppSettings.h"
#include "Workspace/OwnStateChanges.h"

#include "Core/Base/Log.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Fs/FileSystem.h"
#include "Core/Platform/FileDialog.h"

#include <algorithm>
#include <format>
#include <vector>

namespace Graphite {

	namespace {
		template<typename Visitor>
		void ForEachProject(const Folder& folder, Visitor&& visit)
		{
			for (const Project& project : folder.Projects)
				visit(project);

			for (const Folder& child : folder.Folders)
				ForEachProject(child, visit);
		}
	}

	Core::Result<void> Workspace::OpenFolder(const std::filesystem::path& folderPath)
	{
		// Every path in the tree is derived from this one, and the watcher matches its
		// reported paths against tree nodes, so the whole app has to be on one spelling.
		const std::filesystem::path root = Core::NormalizeDirectory(folderPath);

		Core::FileTree candidate;

		// Built first: a failed open must leave the current tree untouched.
		if (const auto rooted = candidate.SetRoot(root); !rooted)
		{
			APP_ERROR("Cannot open folder '{}': {}", Core::ToUtf8(root), rooted.GetError().Message);
			return rooted;
		}

		if (const auto expanded = candidate.Expand(candidate.GetRoot()); !expanded)
		{
			APP_ERROR("Cannot read folder '{}': {}", Core::ToUtf8(root), expanded.GetError().Message);
			return expanded;
		}

		m_Tree = std::move(candidate);
		m_Root = root;
		m_SelectedFile.clear();

		TryOpenRepository();
		m_Tree.Refresh(m_Tree.GetRoot());

		// A new folder gets a fresh resume budget.
		m_WatcherResumed = false;

		StartWatching(root, "");

		m_Settings = WorkspaceSettings{root};
		m_SelectedSolution = Core::FromUtf8(m_Settings.Read("selectedSolution"));

		FindSolutions();
		LoadSolution();

		AppSettings::SaveLastFolder(root);
		m_Events.Publish(WorkspaceOpenedEvent{root});

		APP_INFO("Workspace opened: '{}'", Core::ToUtf8(root));
		return Core::Result<void>::Ok();
	}

	void Workspace::StartWatching(const std::filesystem::path& root,
								  std::string_view failureContext)
	{
		m_Watcher.reset();

		auto watcher = Core::DirectoryWatcher::Start(root);

		if (!watcher)
		{
			// Not fatal: the tree still works, it just will not notice outside changes.
			APP_WARN("Cannot watch '{}'{}: {}", Core::ToUtf8(root), failureContext,
					 watcher.GetError().Message);
			return;
		}

		m_Watcher = std::move(watcher).GetValue();
	}

	void Workspace::RereadEverything()
	{
		m_Tree.Refresh(m_Tree.GetRoot());

		FindSolutions();
		LoadSolution();
	}

	bool Workspace::IsInsideRoot(const std::filesystem::path& path) const
	{
		if (!IsOpen())
			return false;

		// Compared by path component, not by string prefix: C:\work beside C:\workshop
		// would fool a prefix test.
		const std::filesystem::path relative = path.lexically_relative(m_Root);

		return !relative.empty() && *relative.begin() != "..";
	}

	void Workspace::RememberSolutionCandidate(const std::filesystem::path& path)
	{
		// The file may sit somewhere FindSolutions would never look.
		if (std::find(m_SolutionCandidates.begin(), m_SolutionCandidates.end(), path) !=
			m_SolutionCandidates.end())
		{
			return;
		}

		m_SolutionCandidates.push_back(path);
		std::sort(m_SolutionCandidates.begin(), m_SolutionCandidates.end());
	}

	Core::Result<void> Workspace::OpenSolution(const std::filesystem::path& solutionPath)
	{
		std::error_code code;

		if (!std::filesystem::is_regular_file(solutionPath, code))
		{
			return Core::Error{Core::ErrorCode::NotFound,
							   std::format("no solution file at '{}'", Core::ToUtf8(solutionPath))};
		}

		// Refused before anything is touched: by the time ReadSolution could recognise the
		// mistake the choice is already written to .graphite, where it would come back as a
		// candidate on every later open.
		if (!IsSolutionFile(solutionPath))
		{
			return Core::Error{Core::ErrorCode::Unsupported,
							   std::format("'{}' is not a solution file", Core::ToUtf8(solutionPath))};
		}

		std::filesystem::path chosen = std::filesystem::absolute(solutionPath, code);

		if (code)
			chosen = solutionPath;

		chosen = chosen.lexically_normal();
		chosen.make_preferred();

		if (!IsInsideRoot(chosen))
		{
			if (const auto opened = OpenFolder(chosen.parent_path()); !opened)
				return opened;
		}

		RememberSolutionCandidate(chosen);
		SelectSolution(chosen);

		if (!m_Solution)
		{
			return Core::Error{Core::ErrorCode::InvalidArgument,
							   std::format("cannot read '{}'", Core::ToUtf8(chosen))};
		}

		return Core::Result<void>::Ok();
	}

	void Workspace::Close()
	{
		if (!IsOpen())
			return;

		const std::filesystem::path closed = m_Root;

		// Stopped first: the thread reports into the tree that the next lines replace.
		m_Watcher.reset();

		// Drop the query before the repository it reads, not after: between those two
		// statements the tree would hold a query pointing at an empty optional.
		m_Tree.SetIgnoreQuery({});
		m_Repository.reset();
		m_Tree = Core::FileTree{};
		m_Root.clear();
		m_SelectedFile.clear();

		ResetSolutionState();

		// The explorer's mode dropdown still draws with nothing open, and left pointing at
		// the closed folder it would write into a workspace the user has walked away from.
		m_Settings = WorkspaceSettings{std::filesystem::path{}};

		// An empty stored value reads back as "nothing remembered", so this is how the
		// closed state survives a restart.
		AppSettings::SaveLastFolder({});
		m_Events.Publish(WorkspaceClosedEvent{});

		APP_INFO("Workspace closed: '{}'", Core::ToUtf8(closed));
	}

	void Workspace::SelectFile(const std::filesystem::path& path)
	{
		m_SelectedFile = path;
		m_Events.Publish(FileSelectedEvent{path});
	}

	void Workspace::RequestOpenFile(const std::filesystem::path& path, bool permanent)
	{
		m_Events.Publish(OpenFileRequested{path, permanent});
	}

	void Workspace::Update()
	{
		// Before the watcher check: a dialog is answered whether or not a folder is open.
		m_Dialog.Poll();

		if (!m_Watcher)
			return;

		if (!m_Watcher->IsRunning())
		{
			HandleDeadWatcher();
			return;
		}

		const Core::DirectoryWatcher::Changes changed = m_Watcher->TakeSettled();

		if (changed.MovedTo)
		{
			// To the exclusion of everything else in this batch: the directories it carries
			// are spelled under the old root and match nothing any more.
			HandleRootMoved(*changed.MovedTo);
			return;
		}

		// Published before the tree work below, and on its own path: a branch moving is
		// nothing for the file tree to refresh, and the subscribers that care re-ask git
		// rather than reading anything this function touches.
		if (changed.RepositoryState)
			m_Events.Publish(RepositoryChangedEvent{});

		if (changed.Directories.empty() && !changed.Overflowed)
			return;

		// Settled before the directories are refreshed: installing or dropping the ignore
		// query changes what every node should say, so a refresh that ran first would bake
		// in the previous answer.
		const bool repositoryChanged = SyncRepository();

		if (repositoryChanged || changed.Overflowed)
		{
			if (changed.Overflowed)
				APP_TRACE("Filesystem changes were dropped, refreshing the whole tree");

			RereadEverything();
			m_Events.Publish(WorkspaceFilesChangedEvent{{}, true});
			return;
		}

		// FindNode runs afresh inside RefreshChanged on every iteration, and it has to:
		// Refresh rebuilds the children vector, so any FileNode* collected up front would
		// dangle by the second entry.
		for (const std::filesystem::path& directory : changed.Directories)
			RefreshChanged(directory);

		// Guarded because reloading on every batch means a build writing into bin/
		// re-parses the whole solution.
		if (TouchesSolution(changed.Directories))
			LoadSolution();

		// The IDE writing its own state — workspace.json every couple of seconds while the caret
		// moves — is not news about the folder's files. The tree above still catches up, since
		// .graphite is a directory like any other there, but nobody is told: git status, above
		// all, would otherwise be asked again after every write.
		if (OnlyOwnStateChanged(changed.Directories, m_Root))
		{
			return;
		}

		// Published after the tree has caught up, so a subscriber sees one picture.
		m_Events.Publish(WorkspaceFilesChangedEvent{changed.Directories, false});
	}

	bool Workspace::TouchesSolution(const std::vector<std::filesystem::path>& directories) const
	{
		if (m_SelectedSolution.empty())
			return false;

		const std::filesystem::path solutionDirectory = m_SelectedSolution.parent_path();

		for (const std::filesystem::path& directory : directories)
		{
			if (directory == solutionDirectory)
				return true;

			if (!m_Solution)
				continue;

			// A project's .filters sibling lives in the same directory, so one comparison
			// covers both.
			bool matched = false;

			ForEachProject(m_Solution->Root,
						   [&directory, &matched](const Project& project) {
							   if (project.Path.parent_path() == directory)
								   matched = true;
						   });

			if (matched)
				return true;
		}

		return false;
	}

	void Workspace::Reload()
	{
		if (!m_Tree.HasRoot())
			return;

		// Settled before the tree is re-read: installing or dropping the ignore query
		// changes what every node should say, and a refresh that ran first would bake in
		// the previous answer.
		(void)SyncRepository();

		RereadEverything();
	}

	bool Workspace::HasRepositoryVanished() const
	{
		// A bare repository has no working directory, so there is no .git beside it to
		// look for and the probe below would resolve against the process's own directory.
		if (m_Repository->GetWorkingDirectory().empty())
			return false;

		// Against the repository's own working directory rather than m_Root: the workspace
		// can be a subdirectory of the repository, where m_Root/.git never existed.
		std::error_code code;
		const bool present =
			std::filesystem::exists(m_Repository->GetWorkingDirectory() / ".git", code);

		// A stat error is not an answer of "gone": acting on it would drop a perfectly
		// good repository.
		return !code && !present;
	}

	bool Workspace::SyncRepository()
	{
		if (!m_Repository)
		{
			// A `git init` in a terminal.
			TryOpenRepository();
			return m_Repository.has_value();
		}

		if (!HasRepositoryVanished())
			return false;

		// libgit2 does not notice this on its own: with .git gone it keeps answering from
		// the handle it already holds, reading .gitignore straight out of the working tree.
		APP_INFO("The git repository at '{}' is gone",
				 Core::ToUtf8(m_Repository->GetWorkingDirectory()));

		m_Tree.SetIgnoreQuery({});
		m_Repository.reset();

		return true;
	}

	void Workspace::HandleDeadWatcher()
	{
		// With its thread gone the object reports nothing for the rest of the session, and
		// clearing it makes the next frame return early instead of arriving back here.
		m_Watcher.reset();

		if (m_WatcherResumed)
		{
			APP_WARN("Giving up watching '{}' for changes: it failed twice",
					 Core::ToUtf8(m_Root));
			return;
		}

		m_WatcherResumed = true;

		auto watcher = Core::DirectoryWatcher::Start(m_Root);

		if (!watcher)
		{
			// The tree stays as it is rather than being closed: a stale view the user can
			// act on beats one that vanishes without being asked.
			APP_WARN("Cannot resume watching '{}': {}", Core::ToUtf8(m_Root),
					 watcher.GetError().Message);
			return;
		}

		m_Watcher = std::move(watcher).GetValue();

		// Whatever happened during the blind window is gone, and .git may well be what
		// disappeared, so the repository is settled before the tree is re-read.
		(void)SyncRepository();
		m_Tree.Refresh(m_Tree.GetRoot());

		APP_INFO("Resumed watching '{}' after a failure", Core::ToUtf8(m_Root));
	}

	void Workspace::HandleRootMoved(const std::filesystem::path& destination)
	{
		const std::filesystem::path from = m_Root;
		const std::filesystem::path to = Core::NormalizeDirectory(destination);

		APP_INFO("Workspace moved from '{}' to '{}'", Core::ToUtf8(from), Core::ToUtf8(to));

		m_Root = to;
		m_Tree.Rebase(to);

		if (!m_SelectedFile.empty())
		{
			const std::filesystem::path relative = m_SelectedFile.lexically_relative(from);

			m_SelectedFile = relative.empty() ? std::filesystem::path{} : to / relative;
		}

		// The watcher's handle still works — it followed the directory, which is how the
		// move was noticed — but the copy of the root inside its thread is stale, and that
		// copy is what every reported path is built from.
		m_WatcherResumed = false;

		StartWatching(to, " after the move");

		// libgit2 bakes the path into the handle, so the repository has to be opened again
		// from the new location whatever the old one said.
		TryOpenRepository();

		// The settings store and every solution path hold absolute paths under the old
		// root, so both are rebuilt rather than rewritten.
		m_Settings = WorkspaceSettings{to};
		ResetSolutionState();
		m_SelectedSolution = Core::FromUtf8(m_Settings.Read("selectedSolution"));

		RereadEverything();

		AppSettings::SaveLastFolder(to);
		m_Events.Publish(WorkspaceRootMovedEvent{from, to});
	}

	void Workspace::RefreshChanged(const std::filesystem::path& directory)
	{
		Core::FileNode* node = FindNode(directory);

		// A directory that was never expanded has nothing loaded to correct: it reads the
		// current state whenever the user opens it.
		if (node != nullptr && node->IsDirectory && node->Loaded)
			m_Tree.Refresh(*node);
	}

	Core::FileNode* Workspace::FindNode(const std::filesystem::path& path)
	{
		if (!m_Tree.HasRoot())
			return nullptr;

		// An explicit stack rather than recursion: the depth is user-supplied input.
		std::vector<Core::FileNode*> pending{&m_Tree.GetRoot()};

		while (!pending.empty())
		{
			Core::FileNode* node = pending.back();
			pending.pop_back();

			if (node->Path == path)
				return node;

			for (Core::FileNode& child : node->Children)
			{
				if (child.IsDirectory)
					pending.push_back(&child);
			}
		}

		return nullptr;
	}

	void Workspace::TryOpenRepository()
	{
		m_Tree.SetIgnoreQuery({});
		m_Repository.reset();

		auto repository = Core::Repository::Open(m_Root);

		if (!repository)
		{
			APP_TRACE("'{}' is not inside a git repository: {}", Core::ToUtf8(m_Root),
					  repository.GetError().Message);
			return;
		}

		m_Repository.emplace(std::move(repository).GetValue());

		m_Tree.SetIgnoreQuery(
			[this](const std::filesystem::path& path, bool) -> bool { return m_Repository && m_Repository->IsIgnored(path); });

		APP_INFO("Git repository detected at '{}'", Core::ToUtf8(m_Repository->GetWorkingDirectory()));
	}

	void Workspace::FindSolutions()
	{
		m_SolutionCandidates.clear();

		const auto entries = Core::ReadDirectory(m_Root);

		if (!entries)
			return;

		std::vector<std::filesystem::path> legacy;

		for (const Core::DirectoryEntry& entry : entries.GetValue())
		{
			if (entry.IsDirectory)
				continue;

			if (entry.Path.extension() == ".slnx")
				m_SolutionCandidates.push_back(entry.Path);
			else if (entry.Path.extension() == ".sln")
				legacy.push_back(entry.Path);
		}

		// A .sln beside a .slnx of the same name is the same solution in the older format,
		// and the .slnx is what Visual Studio writes now.
		for (const std::filesystem::path& candidate : legacy)
		{
			const bool supersededByXml =
				std::any_of(m_SolutionCandidates.begin(), m_SolutionCandidates.end(),
							[&candidate](const std::filesystem::path& xml) {
								return xml.stem() == candidate.stem();
							});

			if (!supersededByXml)
				m_SolutionCandidates.push_back(candidate);
		}

		KeepSelectedSolutionListed();

		std::sort(m_SolutionCandidates.begin(), m_SolutionCandidates.end());
	}

	void Workspace::KeepSelectedSolutionListed()
	{
		// A solution the user picked by hand can live outside the root, where the scan
		// above would never find it, and dropping it would empty the chooser for no
		// reason the user could see.
		if (m_SelectedSolution.empty())
			return;

		if (std::find(m_SolutionCandidates.begin(), m_SolutionCandidates.end(),
					  m_SelectedSolution) != m_SolutionCandidates.end())
		{
			return;
		}

		std::error_code exists;

		if (std::filesystem::is_regular_file(m_SelectedSolution, exists))
			m_SolutionCandidates.push_back(m_SelectedSolution);
	}

	void Workspace::LoadSolution()
	{
		if (m_SolutionCandidates.empty())
		{
			m_Solution.reset();
			m_SelectedSolution.clear();
			ForgetSolutionAttempt();
			return;
		}

		const bool remembered =
			std::find(m_SolutionCandidates.begin(), m_SolutionCandidates.end(),
					  m_SelectedSolution) != m_SolutionCandidates.end();

		// A remembered choice that is no longer on disk falls back to the first rather
		// than leaving the view empty for a reason the user cannot see.
		if (!remembered)
			m_SelectedSolution = m_SolutionCandidates.front();

		const std::filesystem::file_time_type stamp = ComputeSolutionStamp();

		if (m_SolutionAttempted && m_AttemptedSolution == m_SelectedSolution &&
			stamp == m_SolutionStamp)
		{
			return;
		}

		m_SolutionAttempted = true;
		m_AttemptedSolution = m_SelectedSolution;

		// Recorded before the parse, so a failure is not retried at the same stamp on
		// every focus change; replaced below once a success has named the projects.
		m_SolutionStamp = stamp;
		m_Solution.reset();

		auto solution = ReadSolution(m_SelectedSolution);

		if (!solution)
		{
			APP_WARN("Cannot read '{}': {}", Core::ToUtf8(m_SelectedSolution),
					 solution.GetError().Message);
			return;
		}

		m_Solution.emplace(std::move(solution).GetValue());

		// Recomputed now that m_Solution can name the projects: the stamp taken above saw
		// the solution file alone, so keeping it would make the very next call parse again.
		m_SolutionStamp = ComputeSolutionStamp();

		APP_INFO("Solution '{}' loaded with {} projects", m_Solution->Name,
				 m_Solution->ProjectCount);
	}

	std::filesystem::file_time_type Workspace::ComputeSolutionStamp() const
	{
		std::error_code code;
		auto newest = std::filesystem::last_write_time(m_SelectedSolution, code);

		if (code)
			newest = {};

		if (!m_Solution)
			return newest;

		// The project files count too: editing a .vcxproj changes what the tree should
		// show and leaves the .slnx untouched.
		ForEachProject(m_Solution->Root, [&newest](const Project& project) {
			const auto carryNewest = [&newest](const std::filesystem::path& path) {
				std::error_code code;
				const auto stamp = std::filesystem::last_write_time(path, code);

				if (!code && stamp > newest)
					newest = stamp;
			};

			carryNewest(project.Path);

			// The .filters sibling is half of what the reader built the folders from.
			std::filesystem::path filters = project.Path;
			filters += ".filters";

			carryNewest(filters);
		});

		return newest;
	}

	void Workspace::ForgetSolutionAttempt()
	{
		m_SolutionAttempted = false;
		m_AttemptedSolution.clear();
		m_SolutionStamp = {};
	}

	void Workspace::ResetSolutionState()
	{
		m_Solution.reset();
		m_SolutionCandidates.clear();
		m_SelectedSolution.clear();
		ForgetSolutionAttempt();
	}

	void Workspace::SelectSolution(const std::filesystem::path& path)
	{
		m_SelectedSolution = path;
		m_Settings.Write("selectedSolution", Core::ToUtf8(path));

		LoadSolution();
	}

	void Workspace::RegisterCommands(Core::CommandRegistry& commands)
	{
		commands.Add({.Id = "workspace.openFolder",
					  .Label = "Open Folder...",
					  .Run = [this] { OpenFolderInteractively(*this); },
					  .Enabled = [this] { return !IsDialogOpen(); }});

		commands.Add({.Id = "workspace.openSolution",
					  .Label = "Open Solution...",
					  .Run = [this] { OpenSolutionInteractively(*this); },
					  .Enabled = [this] { return !IsDialogOpen(); }});
	}

	// Both answer on a later frame, from Workspace::Update: the dialog runs on a thread of its
	// own. The callbacks can hold the workspace by reference because it owns the dialog.
	void OpenFolderInteractively(Workspace& workspace)
	{
		workspace.Dialog().Start([] { return Core::FileDialog::PickFolder("Open Folder"); },
								 [&workspace](Core::Result<std::filesystem::path> picked) {
									 if (!picked)
									 {
										 APP_ERROR("Folder dialog failed: {}", picked.GetError().Message);
										 return;
									 }

									 const bool cancelled = picked.GetValue().empty();

									 if (cancelled)
										 return;

									 // OpenFolder logs its own failure and leaves the current folder open.
									 static_cast<void>(workspace.OpenFolder(picked.GetValue()));
								 });
	}

	void OpenSolutionInteractively(Workspace& workspace)
	{
		const std::vector<Core::FileDialog::Filter> filters = {
			{"Visual Studio solutions", "*.sln;*.slnx"},
			{"All files", "*.*"}};

		workspace.Dialog().Start(
			[filters] { return Core::FileDialog::PickFile("Open Solution", filters); },
			[&workspace](Core::Result<std::filesystem::path> picked) {
				if (!picked)
				{
					APP_ERROR("Cannot open the file picker: {}", picked.GetError().Message);
					return;
				}

				const bool cancelled = picked.GetValue().empty();

				if (cancelled)
					return;

				if (const auto opened = workspace.OpenSolution(picked.GetValue()); !opened)
					APP_ERROR("Cannot open the solution: {}", opened.GetError().Message);
			});
	}
}