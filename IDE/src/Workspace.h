#pragma once

#include "Core/Event/EventBus.h"
#include "Core/Fs/FileTree.h"

#include <filesystem>

namespace ide
{
    /// Published after a folder becomes the workspace root.
    struct WorkspaceOpened
    {
        std::filesystem::path root;
    };

    /// Published when the user picks a file in the tree.
    struct FileSelected
    {
        std::filesystem::path path;
    };

    /// The open folder, its tree, and the selected file. Panels talk to it through
    /// the event bus rather than holding each other.
    class Workspace final
    {
    public:
        explicit Workspace(core::EventBus& events) : m_events(events) {}

        /// Opens a folder as the workspace root. On failure the previous root stays.
        core::Result<void> Open(const std::filesystem::path& root);

        void SelectFile(const std::filesystem::path& path);

        [[nodiscard]] bool IsOpen() const noexcept { return m_tree.HasRoot(); }
        [[nodiscard]] const std::filesystem::path& Root() const noexcept { return m_root; }
        [[nodiscard]] core::FileTree& Tree() noexcept { return m_tree; }
        [[nodiscard]] const std::filesystem::path& SelectedFile() const noexcept
        {
            return m_selectedFile;
        }

    private:
        core::EventBus& m_events;
        core::FileTree m_tree;
        std::filesystem::path m_root;
        std::filesystem::path m_selectedFile;
    };

    /// Shows the system folder picker and opens the chosen folder. Cancelling does
    /// nothing and logs nothing. Shared by the menu item and the panel's button so the
    /// two cannot drift apart.
    void OpenFolderInteractively(Workspace& workspace);
}
