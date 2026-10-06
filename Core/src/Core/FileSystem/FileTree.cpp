#include "corepch.h"

#include "Core/FileSystem/FileTree.h"

#include "Core/Base/Log.h"
#include "Core/FileSystem/FileSystem.h"

#include <algorithm>
#include <cstddef>
#include <optional>

namespace Core {

	namespace {

		// filename() of a drive root such as D:/ is empty, so the whole path names it rather
		// than leaving a blank row.
		std::string NodeNameForRootFolder(const std::filesystem::path& rootFolder)
		{
			const std::string folderName = ToUtf8(rootFolder.filename());

			if (folderName.empty())
			{
				return ToUtf8(rootFolder);
			}

			return folderName;
		}

		std::optional<std::size_t> FindNodeWithPath(const std::vector<FileNode>& nodes, const std::filesystem::path& path)
		{
			for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
			{
				if (nodes[nodeIndex].Path == path)
				{
					return nodeIndex;
				}
			}

			return std::nullopt;
		}
	}

	bool IsFileNodeListedBefore(const FileNode& left, const FileNode& right)
	{
		const bool onlyOneIsDirectory = left.IsDirectory != right.IsDirectory;

		if (onlyOneIsDirectory)
		{
			return left.IsDirectory;
		}

		return CompareNames(left.Name, right.Name) < 0;
	}

	Result<void> FileTree::SetRoot(const std::filesystem::path& root)
	{
		std::error_code ignoredStatusError;
		const bool rootIsDirectory = std::filesystem::is_directory(root, ignoredStatusError);

		if (!rootIsDirectory)
		{
			return Error{.Code = ErrorCode::InvalidArgument, .Message = std::format("'{}' is not a directory", ToUtf8(root))};
		}

		m_Root = FileNode{};
		m_Root.Path = root;
		m_Root.Name = NodeNameForRootFolder(root);
		m_Root.IsDirectory = true;
		m_HasRoot = true;

		CORE_INFO("Workspace root set to '{}'", ToUtf8(root));
		return Result<void>::Ok();
	}

	bool FileTree::IsIgnoredByQuery(const std::filesystem::path& path, bool isDirectory) const
	{
		if (!m_IgnoreQuery)
		{
			return false;
		}

		return m_IgnoreQuery(path, isDirectory);
	}

	Result<void> FileTree::Expand(FileNode& node)
	{
		if (!node.IsDirectory)
		{
			return Error{.Code = ErrorCode::InvalidArgument, .Message = "cannot expand a file node"};
		}

		node.Expanded = true;

		if (node.ChildrenLoaded)
		{
			return Result<void>::Ok();
		}

		const Result<std::vector<DirectoryEntry>> listing = ReadDirectory(node.Path);
		node.ChildrenLoaded = true;

		if (!listing)
		{
			node.Readable = false;
			node.Children.clear();
			CORE_WARN("Cannot read '{}': {}", ToUtf8(node.Path), listing.GetError().Message);
			return Result<void>::Ok();
		}

		node.Readable = true;
		node.Children.clear();
		node.Children.reserve(listing.GetValue().size());

		for (const DirectoryEntry& entry : listing.GetValue())
		{
			node.Children.push_back(UnloadedNodeFor(entry));
		}

		std::sort(node.Children.begin(), node.Children.end(), IsFileNodeListedBefore);
		return Result<void>::Ok();
	}

	FileNode FileTree::UnloadedNodeFor(const DirectoryEntry& entry) const
	{
		return FileNode{
			.Path = entry.Path,
			.Name = entry.Name,
			.IsDirectory = entry.IsDirectory,
			.Ignored = IsIgnoredByQuery(entry.Path, entry.IsDirectory),
		};
	}

	void FileTree::Collapse(FileNode& node)
	{
		node.Expanded = false;
	}

	void FileTree::Refresh(FileNode& node)
	{
		if (!node.IsDirectory || !node.ChildrenLoaded)
		{
			return;
		}

		const Result<std::vector<DirectoryEntry>> listing = ReadDirectory(node.Path);

		if (!listing)
		{
			node.Readable = false;
			node.Children.clear();
			CORE_WARN("Cannot refresh '{}': {}", ToUtf8(node.Path), listing.GetError().Message);
			return;
		}

		node.Readable = true;

		std::vector<FileNode> previousChildren = std::move(node.Children);
		node.Children.clear();
		node.Children.reserve(listing.GetValue().size());

		for (const DirectoryEntry& entry : listing.GetValue())
		{
			const std::optional<std::size_t> previousIndex = FindNodeWithPath(previousChildren, entry.Path);
			const bool isSameEntryAsBefore = previousIndex.has_value() && previousChildren[*previousIndex].IsDirectory == entry.IsDirectory;

			if (isSameEntryAsBefore)
			{
				node.Children.push_back(std::move(previousChildren[*previousIndex]));
				node.Children.back().Ignored = IsIgnoredByQuery(entry.Path, entry.IsDirectory);
				previousChildren.erase(previousChildren.begin() + static_cast<std::ptrdiff_t>(*previousIndex));
				continue;
			}

			node.Children.push_back(UnloadedNodeFor(entry));
		}

		std::sort(node.Children.begin(), node.Children.end(), IsFileNodeListedBefore);

		for (FileNode& child : node.Children)
		{
			if (child.IsDirectory && child.ChildrenLoaded)
			{
				Refresh(child);
			}
		}
	}

	void FileTree::Rebase(const std::filesystem::path& newRoot)
	{
		if (!m_HasRoot)
		{
			return;
		}

		const std::filesystem::path normalizedRoot = NormalizeDirectory(newRoot);

		RebaseNode(m_Root, normalizedRoot);
		m_Root.Name = NodeNameForRootFolder(normalizedRoot);
	}

	// Swapping the old root prefix for the new one inside each path string would quietly do nothing when the two spellings differ in case or separators.
	void FileTree::RebaseNode(FileNode& node, const std::filesystem::path& newPath)
	{
		node.Path = newPath;

		for (FileNode& child : node.Children)
		{
			RebaseNode(child, newPath / child.Name);
		}
	}
}