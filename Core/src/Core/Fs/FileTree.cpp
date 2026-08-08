#include "corepch.h"

#include "Core/Fs/FileTree.h"

#include "Core/Base/Log.h"
#include "Core/Fs/FileSystem.h"

#include <algorithm>
#include <cctype>

namespace core
{
    bool FileNodeLess(const FileNode& left, const FileNode& right)
    {
        if (left.isDirectory != right.isDirectory)
            return left.isDirectory;

        return fs::CompareNames(left.name, right.name) < 0;
    }

    Result<void> FileTree::SetRoot(const std::filesystem::path& root)
    {
        std::error_code code;
        if (!std::filesystem::is_directory(root, code))
        {
            return Error{ErrorCode::InvalidArgument,
                         std::format("'{}' is not a directory", root.string())};
        }

        m_root = FileNode{};
        m_root.path = root;
        m_root.name = fs::ToUtf8(root.filename());

        if (m_root.name.empty())
            m_root.name = fs::ToUtf8(root);

        m_root.isDirectory = true;
        m_hasRoot = true;

        CORE_INFO("Workspace root set to '{}'", root.string());
        return Result<void>::Ok();
    }

    bool FileTree::AskIgnored(const std::filesystem::path& path, bool isDirectory) const
    {
        // No query means no opinion, which is the right answer for a folder that is not
        // a repository at all.
        return m_ignoreQuery ? m_ignoreQuery(path, isDirectory) : false;
    }

    Result<void> FileTree::Expand(FileNode& node)
    {
        if (!node.isDirectory)
            return Error{ErrorCode::InvalidArgument, "cannot expand a file node"};

        node.expanded = true;

        if (node.loaded)
            return Result<void>::Ok();

        auto entries = fs::ReadDirectory(node.path);
        node.loaded = true;

        if (!entries)
        {
            // One unreadable folder must not break browsing the rest of the tree.
            node.accessible = false;
            node.children.clear();
            CORE_WARN("Cannot read '{}': {}", node.path.string(), entries.GetError().message);
            return Result<void>::Ok();
        }

        node.accessible = true;
        node.children.clear();
        node.children.reserve(entries.Value().size());

        for (const fs::DirectoryEntry& entry : entries.Value())
        {
            FileNode child;
            child.path = entry.path;
            child.name = entry.name;
            child.isDirectory = entry.isDirectory;
            child.ignored = AskIgnored(entry.path, entry.isDirectory);
            node.children.push_back(std::move(child));
        }

        std::sort(node.children.begin(), node.children.end(), FileNodeLess);
        return Result<void>::Ok();
    }

    void FileTree::Collapse(FileNode& node)
    {
        node.expanded = false;
    }

    void FileTree::Refresh(FileNode& node)
    {
        if (!node.isDirectory || !node.loaded)
            return;

        auto entries = fs::ReadDirectory(node.path);

        if (!entries)
        {
            node.accessible = false;
            node.children.clear();
            CORE_WARN("Cannot refresh '{}': {}", node.path.string(), entries.GetError().message);
            return;
        }

        node.accessible = true;

        std::vector<FileNode> previous = std::move(node.children);
        node.children.clear();
        node.children.reserve(entries.Value().size());

        for (const fs::DirectoryEntry& entry : entries.Value())
        {
            const auto match = std::find_if(previous.begin(), previous.end(),
                                            [&entry](const FileNode& candidate)
                                            { return candidate.path == entry.path; });

            if (match != previous.end() && match->isDirectory == entry.isDirectory)
            {
                // Carry the whole subtree over, then let the recursion below bring it
                // up to date. This is what keeps the user's open folders open.
                node.children.push_back(std::move(*match));
                node.children.back().ignored = AskIgnored(entry.path, entry.isDirectory);
                previous.erase(match);
                continue;
            }

            FileNode child;
            child.path = entry.path;
            child.name = entry.name;
            child.isDirectory = entry.isDirectory;
            child.ignored = AskIgnored(entry.path, entry.isDirectory);
            node.children.push_back(std::move(child));
        }

        std::sort(node.children.begin(), node.children.end(), FileNodeLess);

        for (FileNode& child : node.children)
        {
            if (child.isDirectory && child.loaded)
                Refresh(child);
        }
    }

    void FileTree::Rebase(const std::filesystem::path& newRoot)
    {
        if (!m_hasRoot)
            return;

        const std::filesystem::path normalized = fs::NormalizeDirectory(newRoot);

        RebaseNode(m_root, normalized);

        // The root is the one node whose name a move can change — a child keeps its name
        // and only moves underneath. Without this the tree would go on displaying the
        // folder's previous name while every path beneath it was already correct.
        //
        // The fallback mirrors SetRoot: a drive root has no filename component, and an
        // empty label would render as a blank row.
        m_root.name = fs::ToUtf8(normalized.filename());

        if (m_root.name.empty())
            m_root.name = fs::ToUtf8(normalized);
    }

    void FileTree::RebaseNode(FileNode& node, const std::filesystem::path& newPath)
    {
        node.path = newPath;

        // Rebuilt from the parent's new path and the child's own name rather than by
        // string surgery on the old path: the two spellings only have to agree on the
        // part being replaced, and a prefix replacement would quietly do nothing if they
        // differed in case or separators.
        for (FileNode& child : node.children)
            RebaseNode(child, newPath / child.name);
    }
}
