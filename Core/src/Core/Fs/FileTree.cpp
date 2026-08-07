#include "corepch.h"

#include "Core/Fs/FileTree.h"

#include "Core/Base/Log.h"
#include "Core/Fs/FileSystem.h"

#include <algorithm>
#include <cctype>

namespace core
{
    namespace
    {
        int CompareCaseInsensitive(const std::string& left, const std::string& right)
        {
            const std::size_t count = std::min(left.size(), right.size());

            for (std::size_t i = 0; i < count; ++i)
            {
                const int a = std::tolower(static_cast<unsigned char>(left[i]));
                const int b = std::tolower(static_cast<unsigned char>(right[i]));

                if (a != b)
                    return a < b ? -1 : 1;
            }

            if (left.size() == right.size())
                return 0;

            return left.size() < right.size() ? -1 : 1;
        }
    }

    bool FileNodeLess(const FileNode& left, const FileNode& right)
    {
        if (left.isDirectory != right.isDirectory)
            return left.isDirectory;

        return CompareCaseInsensitive(left.name, right.name) < 0;
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
        m_root.name = root.filename().string();

        if (m_root.name.empty())
            m_root.name = root.string();

        m_root.isDirectory = true;
        m_hasRoot = true;

        CORE_INFO("Workspace root set to '{}'", root.string());
        return Result<void>::Ok();
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
            node.children.push_back(std::move(child));
        }

        std::sort(node.children.begin(), node.children.end(), FileNodeLess);
        return Result<void>::Ok();
    }

    void FileTree::Collapse(FileNode& node)
    {
        node.expanded = false;
    }
}
