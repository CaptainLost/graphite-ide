#pragma once

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"

#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Core {

	struct FileNode
	{
		std::filesystem::path path;
		std::string name;
		bool isDirectory = false;
		bool expanded = false;
		bool loaded = false;	// children have been read from disk
		bool accessible = true; // false when the directory could not be read
		bool ignored = false;	// the ignore query said to treat this as excluded
		std::vector<FileNode> children;
	};

	using IgnoreQuery = std::function<bool(const std::filesystem::path& path, bool isDirectory)>;

	class FileTree final : NonCopyable
	{
	public:
		Result<void> SetRoot(const std::filesystem::path& root);

		Result<void> Expand(FileNode& node);

		void Collapse(FileNode& node);

		void SetIgnoreQuery(IgnoreQuery query) { m_IgnoreQuery = std::move(query); }

		void Refresh(FileNode& node);

		void Rebase(const std::filesystem::path& newRoot);

		bool HasRoot() const { return m_HasRoot; }
		FileNode& GetRoot() { return m_Root; }
		const FileNode& GetRoot() const { return m_Root; }

	private:
		bool AskIgnored(const std::filesystem::path& path, bool isDirectory) const;

		static void RebaseNode(FileNode& node, const std::filesystem::path& newPath);

		FileNode m_Root;
		bool m_HasRoot = false;
		IgnoreQuery m_IgnoreQuery;
	};

	bool FileNodeLess(const FileNode& left, const FileNode& right);
}