#include "Workspace.h"

#include "Settings.h"

#include "Core/Base/Log.h"
#include "Core/Platform/FileDialog.h"

namespace ide
{
    core::Result<void> Workspace::Open(const std::filesystem::path& requested)
    {
        // Normalise once, here, where a path enters the workspace. A command-line
        // argument or a settings file can carry forward slashes, and every path in the
        // tree is derived from this one — so a single conversion keeps the whole app,
        // including the system file browser, on native separators.
        std::filesystem::path root = requested.lexically_normal();
        root.make_preferred();

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

        // A folder that is not a repository is completely normal, so a failure here is
        // logged at trace level and leaves the tree with no ignore query at all.
        m_repository.reset();
        m_tree.SetIgnoreQuery({});

        if (auto repository = core::git::Repository::Open(root); repository)
        {
            m_repository.emplace(std::move(repository).Value());

            m_tree.SetIgnoreQuery(
                [this](const std::filesystem::path& path, bool) -> bool
                { return m_repository->IsIgnored(path); });

            m_tree.Refresh(m_tree.Root());

            CORE_INFO("Git repository detected at '{}'", m_repository->WorkingDirectory().string());
        }
        else
        {
            CORE_TRACE("'{}' is not inside a git repository: {}", root.string(),
                       repository.GetError().message);
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

        m_repository.reset();
        m_tree.SetIgnoreQuery({});
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
