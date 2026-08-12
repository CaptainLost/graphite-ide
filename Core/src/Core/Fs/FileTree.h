#pragma once

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"

#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Core
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
        bool ignored = false;      // the ignore query said to treat this as excluded
        std::vector<FileNode> children;
    };

    /// Answers whether a path should be treated as excluded. FileTree calls this while
    /// loading a directory and caches the answer on the node, because the real
    /// implementation reads rules files off disk and is far too expensive to run per
    /// frame. Supplied by the application: the filesystem layer has no idea git exists.
    using IgnoreQuery = std::function<bool(const std::filesystem::path& path, bool isDirectory)>;

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

        /// Installs the query used from the next load onwards. Nodes already loaded keep
        /// the verdict they were given — refresh them to re-ask. An unset query means
        /// nothing is ignored.
        void SetIgnoreQuery(IgnoreQuery query) { m_IgnoreQuery = std::move(query); }

        /// Re-reads the node from disk, preserving the expansion state of the subtree:
        /// nodes that still exist keep their expanded/loaded flags and their loaded
        /// children, vanished nodes are dropped, and new ones appear collapsed.
        /// A node that was never loaded stays that way — there is nothing to refresh.
        /// A directory that cannot be re-read is not reported through a return value:
        /// it is marked inaccessible (FileNode::accessible = false) and logged instead.
        void Refresh(FileNode& node);

        /// Re-points the tree at the same content in a new location, after the root
        /// directory was renamed or moved on disk. Every node's path is rewritten, and so
        /// is the root's name, since a rename is exactly what changes it. Nothing is read
        /// from disk and nothing else changes, so the expansion the user built up
        /// survives — which is the whole reason this exists rather than simply reopening
        /// the folder.
        ///
        /// Does nothing when there is no root.
        void Rebase(const std::filesystem::path& newRoot);

        [[nodiscard]] bool HasRoot() const noexcept { return m_HasRoot; }
        [[nodiscard]] FileNode& GetRoot() noexcept { return m_Root; }
        [[nodiscard]] const FileNode& GetRoot() const noexcept { return m_Root; }

    private:
        [[nodiscard]] bool AskIgnored(const std::filesystem::path& path, bool isDirectory) const;

        static void RebaseNode(FileNode& node, const std::filesystem::path& newPath);

        FileNode m_Root;
        bool m_HasRoot = false;
        IgnoreQuery m_IgnoreQuery;
    };

    /// Ordering used inside every directory: directories first, then files, each group
    /// alphabetical and case-insensitive. Exposed for reuse when refreshing.
    bool FileNodeLess(const FileNode& left, const FileNode& right);
}
