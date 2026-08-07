#include "Workspace.h"

#include "Settings.h"

#include "Core/Base/Log.h"
#include "Core/Fs/FileSystem.h"
#include "Core/Platform/FileDialog.h"

#include <vector>

namespace ide
{
    core::Result<void> Workspace::Open(const std::filesystem::path& requested)
    {
        // Normalise once, here, where a path enters the workspace. A command-line
        // argument or a settings file can carry forward slashes or a trailing separator,
        // and every path in the tree is derived from this one — so a single conversion
        // keeps the whole app on one spelling, including the system file browser and the
        // change notifications the watcher matches against tree nodes.
        const std::filesystem::path root = core::fs::NormalizeDirectory(requested);

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

        Settings::SaveLastFolder(root);
        m_events.Publish(WorkspaceOpenedEvent{root});

        CORE_INFO("Workspace opened: '{}'", root.string());
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

        // An empty stored value reads back as "nothing remembered", so this is how the
        // closed state survives a restart.
        Settings::SaveLastFolder({});
        m_events.Publish(WorkspaceClosedEvent{});

        CORE_INFO("Workspace closed: '{}'", closed.string());
    }

    void Workspace::SelectFile(const std::filesystem::path& path)
    {
        m_selectedFile = path;
        m_events.Publish(FileSelectedEvent{path});
    }

    void Workspace::Update()
    {
        if (!m_watcher)
            return;

        const core::DirectoryWatcher::Changes changed = m_watcher->TakeSettled();

        if (changed.directories.empty() && !changed.overflowed)
            return;

        if (changed.overflowed)
        {
            // Individual paths were lost, so nothing narrower than the whole tree is
            // trustworthy — which also covers whatever paths this batch did carry.
            // Usually it carries none, so testing the vector first would skip this
            // entirely.
            CORE_TRACE("Filesystem changes were dropped, refreshing the whole tree");
            m_tree.Refresh(m_tree.Root());
        }
        else
        {
            // FindNode runs afresh inside RefreshChanged on every iteration, and it has
            // to: Refresh rebuilds the children vector, so any FileNode* collected up
            // front would dangle by the second entry.
            for (const std::filesystem::path& directory : changed.directories)
                RefreshChanged(directory);
        }

        // A `git init` in a terminal shows up as a change in the root. Retried only while
        // there is no repository, so the ordinary case costs one pointer test.
        if (!m_repository)
            TryOpenRepository();
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

        (void)workspace.Open(picked.Value());
    }
}
