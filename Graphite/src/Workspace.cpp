#include "Workspace.h"

#include "AppSettings.h"

#include "Core/Base/Log.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Fs/FileSystem.h"
#include "Core/Platform/FileDialog.h"

#include <algorithm>
#include <format>
#include <vector>

namespace graphite
{
    namespace
    {
        /// Walks every project in the tree. Solution folders nest, so a plain loop over
        /// one level would miss most of them.
        template <typename Visitor>
        void ForEachProject(const core::sln::Folder& folder, Visitor&& visit)
        {
            for (const core::sln::Project& project : folder.projects)
                visit(project);

            for (const core::sln::Folder& child : folder.folders)
                ForEachProject(child, visit);
        }
    }

    core::Result<void> Workspace::OpenFolder(const std::filesystem::path& folderPath)
    {
        // Normalise once, here, where a path enters the workspace. A command-line
        // argument or a settings file can carry forward slashes or a trailing separator,
        // and every path in the tree is derived from this one — so a single conversion
        // keeps the whole app on one spelling, including the system file browser and the
        // change notifications the watcher matches against tree nodes.
        const std::filesystem::path root = core::fs::NormalizeDirectory(folderPath);

        core::FileTree candidate;

        // Build the new tree first: a failed open must leave the current one untouched.
        if (const auto rooted = candidate.SetRoot(root); !rooted)
        {
            CORE_ERROR("Cannot open folder '{}': {}", root.string(), rooted.GetError().message);
            return rooted;
        }

        if (const auto expanded = candidate.Expand(candidate.Root()); !expanded)
        {
            CORE_ERROR("Cannot read folder '{}': {}", root.string(), expanded.GetError().message);
            return expanded;
        }

        m_tree = std::move(candidate);
        m_root = root;
        m_selectedFile.clear();

        TryOpenRepository();
        m_tree.Refresh(m_tree.Root());

        // A new folder gets a fresh resume budget: the previous one having failed says
        // nothing about this one.
        m_watcherResumed = false;

        if (auto watcher = core::DirectoryWatcher::Start(root); watcher)
        {
            m_watcher = std::move(watcher).Value();
        }
        else
        {
            // Not fatal: the tree still works, it just will not notice outside changes.
            CORE_WARN("Cannot watch '{}': {}", root.string(), watcher.GetError().message);
            m_watcher.reset();
        }

        // Rebuilt for the new root before anything reads it: the store is per folder, so
        // the previous workspace's file must not be consulted for this one.
        m_settings = WorkspaceSettings{root};
        m_selectedSolution = core::fs::FromUtf8(m_settings.Get("selectedSolution"));

        FindSolutions();
        LoadSolution();

        AppSettings::SaveLastFolder(root);
        m_events.Publish(WorkspaceOpenedEvent{root});

        CORE_INFO("Workspace opened: '{}'", root.string());
        return core::Result<void>::Ok();
    }

    core::Result<void> Workspace::OpenSolution(const std::filesystem::path& solutionPath)
    {
        std::error_code code;

        if (!std::filesystem::is_regular_file(solutionPath, code))
        {
            return core::Error{core::ErrorCode::NotFound,
                               std::format("no solution file at '{}'", solutionPath.string())};
        }

        // Refused before anything is touched. Recognising the mistake inside ReadSolution
        // would be too late: by then the workspace has been re-rooted, the file recorded
        // as a candidate, and the choice written to .graphite — where it would come back
        // as a candidate on every later open.
        if (!core::sln::IsSolutionFile(solutionPath))
        {
            return core::Error{core::ErrorCode::Unsupported,
                               std::format("'{}' is not a solution file", solutionPath.string())};
        }

        std::filesystem::path chosen = std::filesystem::absolute(solutionPath, code);

        if (code)
            chosen = solutionPath;

        chosen = chosen.lexically_normal();
        chosen.make_preferred();

        // Inside the folder already open? Then the workspace is right as it is, and only
        // the selection changes. This is the common case: an open repository and a
        // solution somewhere within it.
        const std::filesystem::path relative =
            IsOpen() ? chosen.lexically_relative(m_root) : std::filesystem::path{};

        // Compared by path component, not by string prefix: a sibling directory whose
        // name merely begins with the root's — C:\work beside C:\workshop — would fool a
        // prefix test, which this codebase has already been bitten by once.
        const bool inside = !relative.empty() && *relative.begin() != "..";

        if (!inside)
        {
            if (const auto opened = OpenFolder(chosen.parent_path()); !opened)
                return opened;
        }

        // Added by hand rather than waiting for discovery: the file may sit somewhere
        // FindSolutions would never look, and the user has just said they want it.
        if (std::find(m_solutionCandidates.begin(), m_solutionCandidates.end(), chosen)
            == m_solutionCandidates.end())
        {
            m_solutionCandidates.push_back(chosen);
            std::sort(m_solutionCandidates.begin(), m_solutionCandidates.end());
        }

        SelectSolution(chosen);

        if (!m_solution)
        {
            return core::Error{core::ErrorCode::InvalidArgument,
                               std::format("cannot read '{}'", chosen.string())};
        }

        return core::Result<void>::Ok();
    }

    void Workspace::Close()
    {
        if (!IsOpen())
            return;

        const std::filesystem::path closed = m_root;

        // Stopped first: the thread reports into the tree that the next lines replace.
        m_watcher.reset();

        // Drop the query before the repository it reads, not after: between those two
        // statements the tree would hold a query pointing at an empty optional.
        m_tree.SetIgnoreQuery({});
        m_repository.reset();
        m_tree = core::FileTree{};
        m_root.clear();
        m_selectedFile.clear();

        ResetSolutionState();

        // Dropped with the folder it belongs to. Left pointing at the closed folder, the
        // explorer's mode dropdown — which still draws with nothing open — would write
        // into a workspace the user has just walked away from.
        m_settings = WorkspaceSettings{std::filesystem::path{}};

        // An empty stored value reads back as "nothing remembered", so this is how the
        // closed state survives a restart.
        AppSettings::SaveLastFolder({});
        m_events.Publish(WorkspaceClosedEvent{});

        CORE_INFO("Workspace closed: '{}'", closed.string());
    }

    void Workspace::SelectFile(const std::filesystem::path& path)
    {
        m_selectedFile = path;
        m_events.Publish(FileSelectedEvent{path});
    }

    void Workspace::RequestOpenFile(const std::filesystem::path& path, bool permanent)
    {
        m_events.Publish(OpenFileRequested{path, permanent});
    }

    void Workspace::Update()
    {
        if (!m_watcher)
            return;

        if (!m_watcher->IsRunning())
        {
            HandleDeadWatcher();
            return;
        }

        const core::DirectoryWatcher::Changes changed = m_watcher->TakeSettled();

        if (changed.movedTo)
        {
            // Handled to the exclusion of everything else in this batch: the directories
            // it carries are spelled under the old root and match nothing any more.
            HandleRootMoved(*changed.movedTo);
            return;
        }

        if (changed.directories.empty() && !changed.overflowed)
            return;

        // Settled before the directories are refreshed, not after. Installing or dropping
        // the ignore query changes what every node should say, so a refresh that ran
        // first would bake in the previous answer and hold it until some unrelated change
        // came along.
        const bool repositoryChanged = SyncRepository();

        if (repositoryChanged || changed.overflowed)
        {
            // Either every verdict in the tree just became stale, or individual paths
            // were lost. Both mean nothing narrower than the whole tree is trustworthy —
            // which also covers whatever paths this batch did carry.
            if (changed.overflowed)
                CORE_TRACE("Filesystem changes were dropped, refreshing the whole tree");

            m_tree.Refresh(m_tree.Root());

            FindSolutions();
            LoadSolution();
            m_events.Publish(WorkspaceFilesChangedEvent{{}, true});
            return;
        }

        // FindNode runs afresh inside RefreshChanged on every iteration, and it has to:
        // Refresh rebuilds the children vector, so any FileNode* collected up front would
        // dangle by the second entry.
        for (const std::filesystem::path& directory : changed.directories)
            RefreshChanged(directory);

        // Only when the change could plausibly be a solution or project file. Reloading on
        // every batch means a build writing into bin/ re-parses the whole solution, which
        // showed up immediately in practice: opening this repository moments after a build
        // read it four times over.
        if (TouchesSolution(changed.directories))
            LoadSolution();

        // Published after the tree has caught up, so a subscriber that looks at both sees
        // one consistent picture rather than a tree still describing the previous frame.
        m_events.Publish(WorkspaceFilesChangedEvent{changed.directories, false});
    }

    bool Workspace::TouchesSolution(const std::vector<std::filesystem::path>& directories) const
    {
        if (m_selectedSolution.empty())
            return false;

        const std::filesystem::path solutionDirectory = m_selectedSolution.parent_path();

        for (const std::filesystem::path& directory : directories)
        {
            // The solution's own directory covers the .slnx itself and anything beside it.
            if (directory == solutionDirectory)
                return true;

            if (!m_solution)
                continue;

            // A project file changing means the project's contents may have changed. Its
            // .filters sibling lives in the same directory, so one comparison covers both.
            bool matched = false;

            ForEachProject(m_solution->root,
                           [&directory, &matched](const core::sln::Project& project)
                           {
                               if (project.path.parent_path() == directory)
                                   matched = true;
                           });

            if (matched)
                return true;
        }

        return false;
    }

    void Workspace::Reload()
    {
        if (!m_tree.HasRoot())
            return;

        // Settled before the tree is re-read, not after: installing or dropping the
        // ignore query changes what every node should say, and a refresh that ran first
        // would bake in the previous answer.
        (void)SyncRepository();

        m_tree.Refresh(m_tree.Root());

        FindSolutions();
        LoadSolution();
    }

    bool Workspace::SyncRepository()
    {
        if (!m_repository)
        {
            // A `git init` in a terminal. Attempted only while there is none, so the
            // ordinary case costs one optional test.
            TryOpenRepository();
            return m_repository.has_value();
        }

        // A bare repository has no working directory at all, so there is no .git beside
        // it to look for and the probe below would test a relative path against whatever
        // the process's working directory happens to be.
        if (m_repository->WorkingDirectory().empty())
            return false;

        // Checked against the repository's own working directory rather than m_root: the
        // workspace can be a subdirectory of the repository, where m_root/.git never
        // existed in the first place.
        std::error_code code;
        const bool present = std::filesystem::exists(m_repository->WorkingDirectory() / ".git",
                                                     code);

        // `code` being set means the question could not be answered — a denied parent, a
        // network share that blinked, an indexer holding things briefly. That is not the
        // same as an answer of "gone", and acting on it would drop a perfectly good
        // repository and announce it in the log.
        if (code || present)
            return false;

        // libgit2 does not notice this on its own. With .git gone it keeps answering from
        // the handle it already holds, reading .gitignore straight out of the working
        // tree — so entries would stay dimmed for a folder that is no longer a
        // repository. Pinned by a test in GitRepositoryTests.cpp.
        CORE_INFO("The git repository at '{}' is gone",
                  m_repository->WorkingDirectory().string());

        m_tree.SetIgnoreQuery({});
        m_repository.reset();

        return true;
    }

    void Workspace::HandleDeadWatcher()
    {
        // Dropped rather than kept: with its thread gone the object reports nothing for
        // the rest of the session, and clearing it makes the next frame return early
        // instead of arriving back here.
        m_watcher.reset();

        if (m_watcherResumed)
        {
            CORE_WARN("Giving up watching '{}' for changes: it failed twice",
                      m_root.string());
            return;
        }

        m_watcherResumed = true;

        auto watcher = core::DirectoryWatcher::Start(m_root);

        if (!watcher)
        {
            // The usual reason is that the folder was deleted or renamed out from under
            // the workspace. The tree stays as it is rather than being closed: a stale
            // view the user can act on beats one that vanishes without being asked.
            CORE_WARN("Cannot resume watching '{}': {}", m_root.string(),
                      watcher.GetError().message);
            return;
        }

        m_watcher = std::move(watcher).Value();

        // Whatever happened while nothing was watching is simply gone, so the tree is
        // re-read wholesale rather than trusted — and the repository is settled first,
        // because .git may well be what disappeared during the blind window. Refreshing
        // before that would re-ask a repository that is no longer there and bake the
        // stale answer back in.
        (void)SyncRepository();
        m_tree.Refresh(m_tree.Root());

        CORE_INFO("Resumed watching '{}' after a failure", m_root.string());
    }

    void Workspace::HandleRootMoved(const std::filesystem::path& destination)
    {
        const std::filesystem::path from = m_root;
        const std::filesystem::path to = core::fs::NormalizeDirectory(destination);

        CORE_INFO("Workspace moved from '{}' to '{}'", from.string(), to.string());

        m_root = to;
        m_tree.Rebase(to);

        // Rewritten rather than cleared: the same file is still selected, it just lives
        // somewhere else now.
        if (!m_selectedFile.empty())
        {
            const std::filesystem::path relative = m_selectedFile.lexically_relative(from);

            m_selectedFile = relative.empty() ? std::filesystem::path{} : to / relative;
        }

        // The watcher's handle still works — it followed the directory, which is how the
        // move was noticed at all — but the copy of the root inside its thread is stale,
        // and that copy is what every reported path is built from. Restarting is the only
        // way to correct it without reaching into a running thread.
        m_watcherResumed = false;
        m_watcher.reset();

        if (auto watcher = core::DirectoryWatcher::Start(to); watcher)
        {
            m_watcher = std::move(watcher).Value();
        }
        else
        {
            CORE_WARN("Cannot watch '{}' after the move: {}", to.string(),
                      watcher.GetError().message);
        }

        // libgit2 bakes the path into the handle, so the repository has to be opened
        // again from the new location whatever the old one said.
        TryOpenRepository();

        // Nothing was read from disk during the rebase, so this is the first look at the
        // new location — and the ignore query may have just changed too.
        m_tree.Refresh(m_tree.Root());

        // Both hold absolute paths under the old root, so both are rebuilt rather than
        // rewritten. Left alone, the settings store would keep writing into the folder's
        // former location — recreating .graphite under a name the user has abandoned —
        // and every solution path would name a file that no longer exists there.
        m_settings = WorkspaceSettings{to};
        ResetSolutionState();
        m_selectedSolution = core::fs::FromUtf8(m_settings.Get("selectedSolution"));

        FindSolutions();
        LoadSolution();

        AppSettings::SaveLastFolder(to);
        m_events.Publish(WorkspaceRootMovedEvent{from, to});
    }

    void Workspace::RefreshChanged(const std::filesystem::path& directory)
    {
        core::FileNode* node = FindNode(directory);

        // Not found means the directory was never expanded, so there is nothing loaded to
        // correct — it will read the current state whenever the user opens it.
        if (node != nullptr && node->isDirectory && node->loaded)
            m_tree.Refresh(*node);
    }

    core::FileNode* Workspace::FindNode(const std::filesystem::path& path)
    {
        if (!m_tree.HasRoot())
            return nullptr;

        // An explicit stack rather than recursion: a deep tree is user-supplied input and
        // this runs every frame that something changed.
        std::vector<core::FileNode*> pending{&m_tree.Root()};

        while (!pending.empty())
        {
            core::FileNode* node = pending.back();
            pending.pop_back();

            if (node->path == path)
                return node;

            for (core::FileNode& child : node->children)
            {
                if (child.isDirectory)
                    pending.push_back(&child);
            }
        }

        return nullptr;
    }

    void Workspace::TryOpenRepository()
    {
        m_tree.SetIgnoreQuery({});
        m_repository.reset();

        auto repository = core::git::Repository::Open(m_root);

        if (!repository)
        {
            CORE_TRACE("'{}' is not inside a git repository: {}", m_root.string(),
                       repository.GetError().message);
            return;
        }

        m_repository.emplace(std::move(repository).Value());

        m_tree.SetIgnoreQuery(
            [this](const std::filesystem::path& path, bool) -> bool
            { return m_repository && m_repository->IsIgnored(path); });

        CORE_INFO("Git repository detected at '{}'", m_repository->WorkingDirectory().string());
    }

    void Workspace::FindSolutions()
    {
        m_solutionCandidates.clear();

        const auto entries = core::fs::ReadDirectory(m_root);

        if (!entries)
            return;

        std::vector<std::filesystem::path> legacy;

        for (const core::fs::DirectoryEntry& entry : entries.Value())
        {
            if (entry.isDirectory)
                continue;

            if (entry.path.extension() == ".slnx")
                m_solutionCandidates.push_back(entry.path);
            else if (entry.path.extension() == ".sln")
                legacy.push_back(entry.path);
        }

        // A .sln beside a .slnx of the same name is the same solution in the older
        // format, so it is not offered twice. The .slnx wins: it is what Visual Studio
        // 2026 writes, and in this repository the .sln is a stale leftover.
        for (const std::filesystem::path& candidate : legacy)
        {
            const bool supersededByXml =
                std::any_of(m_solutionCandidates.begin(), m_solutionCandidates.end(),
                            [&candidate](const std::filesystem::path& xml)
                            { return xml.stem() == candidate.stem(); });

            if (!supersededByXml)
                m_solutionCandidates.push_back(candidate);
        }

        // A solution the user picked by hand may live outside the root, where nothing
        // above would find it. Dropping it on the next filesystem change would make the
        // chooser lose an entry for no reason the user could see.
        if (!m_selectedSolution.empty()
            && std::find(m_solutionCandidates.begin(), m_solutionCandidates.end(),
                         m_selectedSolution)
                   == m_solutionCandidates.end())
        {
            std::error_code exists;

            if (std::filesystem::is_regular_file(m_selectedSolution, exists))
                m_solutionCandidates.push_back(m_selectedSolution);
        }

        std::sort(m_solutionCandidates.begin(), m_solutionCandidates.end());
    }

    void Workspace::LoadSolution()
    {
        if (m_solutionCandidates.empty())
        {
            m_solution.reset();
            m_selectedSolution.clear();
            ForgetSolutionAttempt();
            return;
        }

        // A remembered choice that is no longer on disk falls back to the first, rather
        // than leaving the view empty for a reason the user cannot see.
        const bool remembered =
            std::find(m_solutionCandidates.begin(), m_solutionCandidates.end(),
                      m_selectedSolution) != m_solutionCandidates.end();

        if (!remembered)
            m_selectedSolution = m_solutionCandidates.front();

        // Guarded here rather than at each caller. There are five — window focus, the
        // watcher, the toolbar, opening a folder, choosing a solution — and gating only
        // one of them left the rest re-parsing on every batch.
        //
        // The stamp covers the project files too, not just the solution: editing a
        // .vcxproj changes what the tree should show and leaves the .slnx untouched, so a
        // solution-only stamp would make the view permanently stale for exactly the edit
        // this feature exists to reflect.
        //
        // Recorded even when the parse fails. Otherwise an unreadable solution re-parses
        // and re-warns on every focus change, which is the same noise the guard removes
        // from the success path.
        const std::filesystem::file_time_type stamp = SolutionStamp();

        if (m_solutionAttempted && m_attemptedSolution == m_selectedSolution
            && stamp == m_solutionStamp)
        {
            return;
        }

        m_solutionAttempted = true;
        m_attemptedSolution = m_selectedSolution;

        // Recorded now so a failed parse is not retried at the same stamp; replaced below
        // once a successful one has told us which projects there are.
        m_solutionStamp = stamp;
        m_solution.reset();

        auto solution = core::sln::ReadSolution(m_selectedSolution);

        if (!solution)
        {
            CORE_WARN("Cannot read '{}': {}", m_selectedSolution.string(),
                      solution.GetError().message);
            return;
        }

        m_solution.emplace(std::move(solution).Value());

        // Recomputed against the model that was just read. The stamp spans the project
        // files as well as the solution, and SolutionStamp finds those through m_solution
        // — which was empty a moment ago, so the value compared above was the solution
        // file alone. Keeping it would make the very next call see a different stamp and
        // parse again, which is exactly what happened: one wasted reload after every
        // start, surfacing at whatever first touched the workspace.
        m_solutionStamp = SolutionStamp();

        CORE_INFO("Solution '{}' loaded with {} projects", m_solution->name,
                  m_solution->projectCount);
    }


    std::filesystem::file_time_type Workspace::SolutionStamp() const
    {
        std::error_code code;
        auto newest = std::filesystem::last_write_time(m_selectedSolution, code);

        if (code)
            newest = {};

        if (!m_solution)
            return newest;

        ForEachProject(m_solution->root,
                       [&newest](const core::sln::Project& project)
                       {
                           std::error_code projectCode;

                           const auto projectStamp =
                               std::filesystem::last_write_time(project.path, projectCode);

                           if (!projectCode && projectStamp > newest)
                               newest = projectStamp;

                           // The .filters sibling is half of what the reader used to build
                           // this project's folders, so a change to it counts as a change
                           // to the project.
                           std::filesystem::path filters = project.path;
                           filters += ".filters";

                           const auto filtersStamp =
                               std::filesystem::last_write_time(filters, projectCode);

                           if (!projectCode && filtersStamp > newest)
                               newest = filtersStamp;
                       });

        return newest;
    }

    void Workspace::ForgetSolutionAttempt()
    {
        m_solutionAttempted = false;
        m_attemptedSolution.clear();
        m_solutionStamp = {};
    }

    void Workspace::ResetSolutionState()
    {
        m_solution.reset();
        m_solutionCandidates.clear();
        m_selectedSolution.clear();
        ForgetSolutionAttempt();
    }

    void Workspace::SelectSolution(const std::filesystem::path& path)
    {
        m_selectedSolution = path;
        m_settings.Set("selectedSolution", core::fs::ToUtf8(path));

        // The guard keys on which solution was attempted, so switching to another is
        // never mistaken for a repeat of the last one.
        LoadSolution();
    }

    void Workspace::RegisterCommands(core::CommandRegistry& commands)
    {
        commands.Add({.id = "workspace.openFolder",
                      .label = "Open Folder...",
                      .run = [this] { OpenFolderInteractively(*this); }});

        commands.Add({.id = "workspace.openSolution",
                      .label = "Open Solution...",
                      .run = [this] { OpenSolutionInteractively(*this); }});
    }

    void OpenFolderInteractively(Workspace& workspace)
    {
        auto picked = core::FileDialog::PickFolder("Open Folder");
        if (!picked)
        {
            CORE_ERROR("Folder dialog failed: {}", picked.GetError().message);
            return;
        }

        // An empty path means the user cancelled, which is not a failure.
        if (picked.Value().empty())
            return;

        (void)workspace.OpenFolder(picked.Value());
    }

    void OpenSolutionInteractively(Workspace& workspace)
    {
        const std::vector<core::FileDialog::Filter> filters = {
            {"Visual Studio solutions", "*.sln;*.slnx"},
            {"All files", "*.*"}};

        auto picked = core::FileDialog::PickFile("Open Solution", filters);

        if (!picked)
        {
            CORE_ERROR("Cannot open the file picker: {}", picked.GetError().message);
            return;
        }

        // An empty path is the user cancelling, which is not worth a log line.
        if (picked.Value().empty())
            return;

        if (const auto opened = workspace.OpenSolution(picked.Value()); !opened)
            CORE_ERROR("Cannot open the solution: {}", opened.GetError().message);
    }
}
