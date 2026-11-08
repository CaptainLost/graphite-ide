#include "corepch.h"

#include "Core/FileSystem/FileTree.h"

#include "Core/Base/Log.h"
#include "Core/FileSystem/FileSystem.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <unordered_map>

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

		struct PathHash
		{
			std::size_t operator()(const std::filesystem::path& path) const { return std::filesystem::hash_value(path); }
		};

		// WARNING: A search of the list for each entry would cost seconds on a folder of thirty thousand entries.
		using NodeIndexByPath = std::unordered_map<std::filesystem::path, std::size_t, PathHash>;

		NodeIndexByPath IndexByPath(const std::vector<FileNode>& nodes)
		{
			NodeIndexByPath indexByPath;
			indexByPath.reserve(nodes.size());

			for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
			{
				indexByPath.emplace(nodes[nodeIndex].Path, nodeIndex);
			}

			return indexByPath;
		}

		std::optional<std::size_t> FindNodeWithPath(const NodeIndexByPath& indexByPath, const std::filesystem::path& path)
		{
			const NodeIndexByPath::const_iterator found = indexByPath.find(path);

			if (found == indexByPath.end())
			{
				return std::nullopt;
			}

			const auto& [foundPath, nodeIndex] = *found;
			return nodeIndex;
		}
	}

	bool IsFileNodeListedBefore(const FileNode& left, const FileNode& right)
	{
		const bool onlyOneIsDirectory = left.IsDirectory != right.IsDirectory;

		if (onlyOneIsDirectory)
		{
			return left.IsDirectory;
		}

		return CompareNamesNaturally(left.Name, right.Name) < 0;
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

	// WARNING: git cannot take back a path inside an ignored folder, so its entries are not asked about.
	bool FileTree::IsIgnoredInside(const FileNode& folder, const DirectoryEntry& entry) const
	{
		if (folder.Ignored)
		{
			return true;
		}

		return IsIgnoredByQuery(entry.Path, entry.IsDirectory);
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
			node.Children.push_back(UnloadedNodeFor(node, entry));
		}

		std::sort(node.Children.begin(), node.Children.end(), IsFileNodeListedBefore);
		return Result<void>::Ok();
	}

	FileNode FileTree::UnloadedNodeFor(const FileNode& folder, const DirectoryEntry& entry) const
	{
		return FileNode{
			.Path = entry.Path,
			.Name = entry.Name,
			.IsDirectory = entry.IsDirectory,
			.Ignored = IsIgnoredInside(folder, entry),
		};
	}

	void FileTree::Collapse(FileNode& node)
	{
		node.Expanded = false;
	}

	void FileTree::Refresh(FileNode& node, IgnoreAnswers ignoreAnswers)
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
		const NodeIndexByPath previousIndexByPath = IndexByPath(previousChildren);
		node.Children.clear();
		node.Children.reserve(listing.GetValue().size());

		for (const DirectoryEntry& entry : listing.GetValue())
		{
			const std::optional<std::size_t> previousIndex = FindNodeWithPath(previousIndexByPath, entry.Path);
			const bool isSameEntryAsBefore = previousIndex.has_value() && previousChildren[*previousIndex].IsDirectory == entry.IsDirectory;

			if (isSameEntryAsBefore)
			{
				node.Children.push_back(std::move(previousChildren[*previousIndex]));

				if (ignoreAnswers == IgnoreAnswers::AskAgain)
				{
					node.Children.back().Ignored = IsIgnoredInside(node, entry);
				}

				continue;
			}

			node.Children.push_back(UnloadedNodeFor(node, entry));
		}

		std::sort(node.Children.begin(), node.Children.end(), IsFileNodeListedBefore);

		for (FileNode& child : node.Children)
		{
			if (child.IsDirectory && child.ChildrenLoaded)
			{
				Refresh(child, ignoreAnswers);
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