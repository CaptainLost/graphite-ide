#pragma once

#include "Core/Event/EventBus.h"
#include "Core/Fs/DirectoryWatcher.h"
#include "Core/Fs/FileTree.h"
#include "Core/Git/Repository.h"

#include <filesystem>
#include <memory>
#include <optional>

namespace ide
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

    /// The open folder was renamed or moved on disk and the workspace followed it.
    /// Anything holding a path below the old root should rewrite it — the tree has
    /// already done so.
    struct WorkspaceRootMovedEvent
    {
        std::filesystem::path from;
        std::filesystem::path to;
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
        core::Result<void> Open(const std::filesystem::path& root);

        /// Drops the open folder, its tree and the selection, and forgets the remembered
        /// folder so the next launch starts empty. Closing is an explicit decision, so it
        /// has to survive a restart. Does nothing when no folder is open.
        void Close();

        /// Applies any filesystem changes noticed since the last frame. Cheap when
        /// nothing happened, which is the overwhelmingly common case.
        void Update();

        /// Re-reads everything the workspace holds: the repository state and every loaded
        /// directory. Cheap enough to run on demand, far too expensive to run per frame.
        ///
        /// Does nothing when no folder is open.
        void Reload();

        void SelectFile(const std::filesystem::path& path);

        [[nodiscard]] bool IsOpen() const noexcept { return m_tree.HasRoot(); }
        [[nodiscard]] const std::filesystem::path& Root() const noexcept { return m_root; }
        [[nodiscard]] core::FileTree& Tree() noexcept { return m_tree; }
        [[nodiscard]] const std::filesystem::path& SelectedFile() const noexcept
        {
            return m_selectedFile;
        }

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
    };

    /// Shows the system folder picker and opens the chosen folder. Cancelling does
    /// nothing and logs nothing. Shared by the menu item and the panel's button so the
    /// two cannot drift apart.
    void OpenFolderInteractively(Workspace& workspace);
}
