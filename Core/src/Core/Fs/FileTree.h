#pragma once

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"

#include <filesystem>
#include <string>
#include <vector>

namespace core
{
    /// One node of the browsed directory tree. Children are read from disk only when
    /// the node is expanded for the first time.
    struct FileNode
    {
        std::filesystem::path path;
        std::string name;
        bool isDirectory = false;
        bool expanded = false;
        bool loaded = false;      // children have been read from disk
        bool accessible = true;   // false when the directory could not be read
        std::vector<FileNode> children;
    };

    /// The tree model behind the file panel. Knows nothing about ImGui, which is what
    /// makes it unit-testable.
    class FileTree final : NonCopyable
    {
    public:
        /// Points the tree at a directory. The root starts collapsed and unloaded.
        Result<void> SetRoot(const std::filesystem::path& root);

        /// Marks the node expanded, reading its children if this is the first time.
        /// A directory that cannot be read is marked inaccessible rather than failing
        /// the call — one unreadable folder must not break browsing the rest.
        Result<void> Expand(FileNode& node);

        void Collapse(FileNode& node);

        /// Re-reads the node from disk, preserving the expansion state of the subtree:
        /// nodes that still exist keep their expanded/loaded flags and their loaded
        /// children, vanished nodes are dropped, and new ones appear collapsed.
        /// A node that was never loaded stays that way — there is nothing to refresh.
        /// A directory that cannot be re-read is not reported through a return value:
        /// it is marked inaccessible (FileNode::accessible = false) and logged instead.
        void Refresh(FileNode& node);

        [[nodiscard]] bool HasRoot() const noexcept { return m_hasRoot; }
        [[nodiscard]] FileNode& Root() noexcept { return m_root; }
        [[nodiscard]] const FileNode& Root() const noexcept { return m_root; }

    private:
        FileNode m_root;
        bool m_hasRoot = false;
    };

    /// Ordering used inside every directory: directories first, then files, each group
    /// alphabetical and case-insensitive. Exposed for reuse when refreshing.
    bool FileNodeLess(const FileNode& left, const FileNode& right);
}
