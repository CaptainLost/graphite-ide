#pragma once

#include "WorkspaceSettings.h"

#include "Core/Event/EventBus.h"
#include "Core/Fs/DirectoryWatcher.h"
#include "Core/Fs/FileTree.h"
#include "Core/Git/Repository.h"
#include "Core/Sln/SolutionReader.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace core
{
    class CommandRegistry;
}

namespace graphite
{
    /// Published after a folder becomes the workspace root.
    struct WorkspaceOpenedEvent
    {
        std::filesystem::path root;
    };

    /// Published after the workspace is closed and no folder is open any more.
    struct WorkspaceClosedEvent
    {
    };

    /// Published when the user picks a file in the tree.
    struct FileSelectedEvent
    {
        std::filesystem::path path;
    };

    /// The user asked for a file to be opened in the editor. Distinct from
    /// FileSelectedEvent: selecting highlights a row, opening puts a window on screen,
    /// and the tree does the first on a single click and the second on a double one.
    ///
    /// `permanent` is false for a single click, which opens a preview - of which there is
    /// at most one, replaced by the next single click.
    struct OpenFileRequested
    {
        std::filesystem::path path;
        bool permanent = false;
    };

    /// The open folder was renamed or moved on disk and the workspace followed it.
    /// Anything holding a path below the old root should rewrite it — the tree has
    /// already done so.
    struct WorkspaceRootMovedEvent
    {
        std::filesystem::path from;
        std::filesystem::path to;
    };

    /// Published once the tree has caught up with a batch of filesystem changes.
    ///
    /// The watcher reports directories, not individual files, so this carries the
    /// directories whose contents changed rather than the paths that changed inside
    /// them. `everything` says the batch was too broad to name any: notifications were
    /// dropped, or the git state moved, and the whole tree was re-read instead.
    ///
    /// Anything holding a path on disk should re-check it — the open documents do,
    /// because a file deleted from Explorer while its tab is open must stop looking like
    /// a file that is still there.
    struct WorkspaceFilesChangedEvent
    {
        std::vector<std::filesystem::path> directories;
        bool everything = false;
    };

    /// The open folder, its tree, and the selected file. Panels talk to it through
    /// the event bus rather than holding each other.
    class Workspace final
    {
    public:
        explicit Workspace(core::EventBus& events) : m_events(events) {}

        /// Neither copyable nor movable: the tree holds a query that captures `this`, so
        /// a moved-from workspace would leave that query pointing at the old object.
        Workspace(Workspace&&) = delete;
        Workspace& operator=(Workspace&&) = delete;

        /// Opens a folder as the workspace root. On failure the previous root stays.
        core::Result<void> OpenFolder(const std::filesystem::path& folderPath);

        /// Opens a solution file directly.
        ///
        /// When the solution already sits inside the open folder, nothing about the
        /// workspace changes and the solution is simply selected. Otherwise the
        /// solution's own directory becomes the workspace root — the file tree, the
        /// watcher and git detection all key off a root, so a solution cannot be opened
        /// without one.
        core::Result<void> OpenSolution(const std::filesystem::path& solutionPath);

        /// Drops the open folder, its tree and the selection, and forgets the remembered
        /// folder so the next launch starts empty. Closing is an explicit decision, so it
        /// has to survive a restart. Does nothing when no folder is open.
        void Close();

        /// Applies any filesystem changes noticed since the last frame. Cheap when
        /// nothing happened, which is the overwhelmingly common case.
        void Update();

        /// Re-reads the repository state and every loaded directory. Cheap enough to run
        /// on demand, far too expensive to run per frame.
        ///
        /// The repository handle is settled, not reopened, so git configuration read at
        /// open time does not come back. In particular, repointing `core.excludesFile` at
        /// a different file has no effect until the folder is reopened — libgit2 caches
        /// that setting in the handle, which a test in GitRepositoryTests.cpp pins.
        /// Editing the file it already points at does take effect.
        ///
        /// Does nothing when no folder is open.
        void Reload();

        void SelectFile(const std::filesystem::path& path);

        /// The user asked for `path` to be opened in the editor - a double click for a
        /// permanent tab, a single click for a preview. Publishes OpenFileRequested;
        /// DocumentWindows is what actually opens it, symmetric with SelectFile publishing
        /// FileSelectedEvent.
        void RequestOpenFile(const std::filesystem::path& path, bool permanent);

        [[nodiscard]] bool IsOpen() const noexcept { return m_tree.HasRoot(); }
        [[nodiscard]] const std::filesystem::path& Root() const noexcept { return m_root; }
        [[nodiscard]] core::FileTree& Tree() noexcept { return m_tree; }
        [[nodiscard]] const std::filesystem::path& SelectedFile() const noexcept
        {
            return m_selectedFile;
        }

        /// The solution being shown, when one was found and parsed.
        ///
        /// Not named Solution(): a member function of that name would shadow the type
        /// core::sln::Solution everywhere inside this class.
        [[nodiscard]] const std::optional<core::sln::Solution>& LoadedSolution() const noexcept
        {
            return m_solution;
        }

        /// The solutions on offer: those found in the workspace root, plus any the user
        /// opened by name, which may live anywhere. More than one puts a chooser in the
        /// panel header.
        [[nodiscard]] const std::vector<std::filesystem::path>& SolutionCandidates() const noexcept
        {
            return m_solutionCandidates;
        }

        /// Switches to one of the candidates and remembers the choice.
        void SelectSolution(const std::filesystem::path& path);

        /// Settings stored in this folder's .graphite directory, as opposed to the
        /// application-wide ones in AppSettings.
        [[nodiscard]] WorkspaceSettings& FolderSettings() noexcept { return m_settings; }

        /// Registers the two ways of opening a workspace. Closing one is the layer's,
        /// not registered here: it goes through the unsaved-work question, which is the
        /// layer's machinery.
        void RegisterCommands(core::CommandRegistry& commands);

    private:
        [[nodiscard]] core::FileNode* FindNode(const std::filesystem::path& path);
        void RefreshChanged(const std::filesystem::path& directory);
        void TryOpenRepository();

        /// Opens or drops the repository to match what is on disk. Returns true when it
        /// changed either way, which means every cached ignore verdict in the tree is now
        /// stale.
        [[nodiscard]] bool SyncRepository();

        /// Tries once to start watching again, then leaves the workspace unwatched.
        void HandleDeadWatcher();

        void HandleRootMoved(const std::filesystem::path& destination);

        void FindSolutions();
        void LoadSolution();

        /// Whether any of the changed directories could hold the solution file or one
        /// of its project files. Everything else — build output above all — must not
        /// cost a re-parse.
        [[nodiscard]] bool TouchesSolution(
            const std::vector<std::filesystem::path>& directories) const;

        /// Newest write time across the solution and the project files it named. Covers
        /// the project files because editing one changes what the tree should show while
        /// leaving the solution file untouched.
        [[nodiscard]] std::filesystem::file_time_type SolutionStamp() const;

        void ForgetSolutionAttempt();
        void ResetSolutionState();

        core::EventBus& m_events;
        /// Declared before m_tree on purpose: the tree holds a query that reads this
        /// repository, and members are destroyed in reverse declaration order, so the
        /// tree must go first.
        std::optional<core::git::Repository> m_repository;
        core::FileTree m_tree;
        /// Declared after m_tree so it is destroyed first: its thread must be stopped
        /// before the tree it reports into goes away.
        std::unique_ptr<core::DirectoryWatcher> m_watcher;

        /// One resume attempt per open folder. A watcher that dies twice is failing for a
        /// reason retrying will not fix, and restarting a thread every frame would be far
        /// worse than not watching at all.
        bool m_watcherResumed = false;
        std::filesystem::path m_root;
        std::filesystem::path m_selectedFile;

        std::optional<core::sln::Solution> m_solution;
        std::vector<std::filesystem::path> m_solutionCandidates;
        std::filesystem::path m_selectedSolution;

        /// Newest write time across the solution file and every project file it named,
        /// as of the last attempt. Reloading is asked for from five directions, and this
        /// is what makes the ones with nothing to do cost a handful of stats.
        std::filesystem::file_time_type m_solutionStamp;

        /// Which solution the stamp belongs to, and whether an attempt was made at all.
        /// Recorded on failure as well as success, so an unreadable solution is not
        /// re-parsed and re-reported every time the window regains focus.
        std::filesystem::path m_attemptedSolution;
        bool m_solutionAttempted = false;

        /// Rebuilt by Open for each root, so it always points at the folder in front of
        /// the user. Default-constructed with an empty root, where reads give nothing and
        /// writes do nothing.
        WorkspaceSettings m_settings{std::filesystem::path{}};
    };

    /// Shows the system folder picker and opens the chosen folder. Cancelling does
    /// nothing and logs nothing. Shared by the menu item and the panel's button so the
    /// two cannot drift apart.
    void OpenFolderInteractively(Workspace& workspace);

    /// Asks for a solution file and opens it. Does nothing when the user cancels.
    void OpenSolutionInteractively(Workspace& workspace);
}
