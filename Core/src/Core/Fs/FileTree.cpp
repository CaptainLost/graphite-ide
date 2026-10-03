#include "corepch.h"

#include "Core/Fs/FileTree.h"

#include "Core/Base/Log.h"
#include "Core/Fs/FileSystem.h"

#include <algorithm>
#include <cctype>

namespace Core {

	bool FileNodeLess(const FileNode& left, const FileNode& right)
	{
		if (left.IsDirectory != right.IsDirectory)
			return left.IsDirectory;

		return CompareNames(left.Name, right.Name) < 0;
	}

	Result<void> FileTree::SetRoot(const std::filesystem::path& root)
	{
		std::error_code code;
		if (!std::filesystem::is_directory(root, code))
		{
			return Error{ErrorCode::InvalidArgument,
						 std::format("'{}' is not a directory", ToUtf8(root))};
		}

		m_Root = FileNode{};
		m_Root.Path = root;
		m_Root.Name = ToUtf8(root.filename());

		if (m_Root.Name.empty())
			m_Root.Name = ToUtf8(root);

		m_Root.IsDirectory = true;
		m_HasRoot = true;

		CORE_INFO("Workspace root set to '{}'", ToUtf8(root));
		return Result<void>::Ok();
	}

	bool FileTree::AskIgnored(const std::filesystem::path& path, bool isDirectory) const
	{
		// No query means no opinion, the right answer for a folder that is not a repository.
		return m_IgnoreQuery ? m_IgnoreQuery(path, isDirectory) : false;
	}

	Result<void> FileTree::Expand(FileNode& node)
	{
		if (!node.IsDirectory)
			return Error{ErrorCode::InvalidArgument, "cannot expand a file node"};

		node.Expanded = true;

		if (node.Loaded)
			return Result<void>::Ok();

		auto entries = ReadDirectory(node.Path);
		node.Loaded = true;

		if (!entries)
		{
			// One unreadable folder must not break browsing the rest of the tree.
			node.Accessible = false;
			node.Children.clear();
			CORE_WARN("Cannot read '{}': {}", ToUtf8(node.Path), entries.GetError().Message);
			return Result<void>::Ok();
		}

		node.Accessible = true;
		node.Children.clear();
		node.Children.reserve(entries.GetValue().size());

		for (const DirectoryEntry& entry : entries.GetValue())
		{
			node.Children.push_back(NodeFor(entry));
		}

		std::sort(node.Children.begin(), node.Children.end(), FileNodeLess);
		return Result<void>::Ok();
	}

	FileNode FileTree::NodeFor(const DirectoryEntry& entry) const
	{
		FileNode node;
		node.Path = entry.Path;
		node.Name = entry.Name;
		node.IsDirectory = entry.IsDirectory;
		node.Ignored = AskIgnored(entry.Path, entry.IsDirectory);
		return node;
	}

	void FileTree::Collapse(FileNode& node)
	{
		node.Expanded = false;
	}

	void FileTree::Refresh(FileNode& node)
	{
		if (!node.IsDirectory || !node.Loaded)
			return;

		auto entries = ReadDirectory(node.Path);

		if (!entries)
		{
			node.Accessible = false;
			node.Children.clear();
			CORE_WARN("Cannot refresh '{}': {}", ToUtf8(node.Path), entries.GetError().Message);
			return;
		}

		node.Accessible = true;

		std::vector<FileNode> previous = std::move(node.Children);
		node.Children.clear();
		node.Children.reserve(entries.GetValue().size());

		for (const DirectoryEntry& entry : entries.GetValue())
		{
			const auto match = std::find_if(previous.begin(), previous.end(),
											[&entry](const FileNode& candidate) { return candidate.Path == entry.Path; });

			if (match != previous.end() && match->IsDirectory == entry.IsDirectory)
			{
				// The whole subtree is carried over and brought up to date by the recursion
				// below, which is what keeps the user's open folders open.
				node.Children.push_back(std::move(*match));
				node.Children.back().Ignored = AskIgnored(entry.Path, entry.IsDirectory);
				previous.erase(match);
				continue;
			}

			node.Children.push_back(NodeFor(entry));
		}

		std::sort(node.Children.begin(), node.Children.end(), FileNodeLess);

		for (FileNode& child : node.Children)
		{
			if (child.IsDirectory && child.Loaded)
				Refresh(child);
		}
	}

	void FileTree::Rebase(const std::filesystem::path& newRoot)
	{
		if (!m_HasRoot)
			return;

		const std::filesystem::path normalized = NormalizeDirectory(newRoot);

		RebaseNode(m_Root, normalized);

		// The root is the one node whose name a move can change; a child keeps its name and
		// only moves underneath. The fallback mirrors SetRoot: a drive root has no filename
		// component, and an empty label would render as a blank row.
		m_Root.Name = ToUtf8(normalized.filename());

		if (m_Root.Name.empty())
			m_Root.Name = ToUtf8(normalized);
	}

	void FileTree::RebaseNode(FileNode& node, const std::filesystem::path& newPath)
	{
		node.Path = newPath;

		// Rebuilt from the parent's new path and the child's own name rather than by string
		// surgery on the old one, which would quietly do nothing if the two spellings
		// differed in case or separators.
		for (FileNode& child : node.Children)
			RebaseNode(child, newPath / child.Name);
	}
}