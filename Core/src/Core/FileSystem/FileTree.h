#pragma once

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Core {

	struct DirectoryEntry;

	struct FileNode
	{
		std::filesystem::path Path;
		std::string Name;
		bool IsDirectory = false;
		bool Expanded = false;
		bool ChildrenLoaded = false;
		bool Readable = true;
		bool Ignored = false;
		std::vector<FileNode> Children;
	};

	using IgnoreQuery = std::function<bool(const std::filesystem::path& path, bool isDirectory)>;

	// Handed a folder's whole listing before any of its entries is asked about, so an answer that
	// costs a round trip can be had for the folder at once.
	using IgnoreFolderQuery = std::function<void(const std::filesystem::path& folder, const std::vector<DirectoryEntry>& entries)>;

	enum class IgnoreAnswers : std::uint8_t
	{
		Keep,
		AskAgain
	};

	class FileTree final : NonCopyable
	{
	public:
		Result<void> SetRoot(const std::filesystem::path& root);

		Result<void> Expand(FileNode& node);

		void Collapse(FileNode& node);

		void SetIgnoreQuery(IgnoreQuery query) { m_IgnoreQuery = std::move(query); }
		void SetIgnoreFolderQuery(IgnoreFolderQuery query) { m_IgnoreFolderQuery = std::move(query); }

		void Refresh(FileNode& node, IgnoreAnswers ignoreAnswers = IgnoreAnswers::Keep);

		void Rebase(const std::filesystem::path& newRoot);

		bool HasRoot() const { return m_HasRoot; }
		FileNode& GetRoot() { return m_Root; }
		const FileNode& GetRoot() const { return m_Root; }

	private:
		bool IsIgnoredByQuery(const std::filesystem::path& path, bool isDirectory) const;

		bool IsIgnoredInside(const FileNode& folder, const DirectoryEntry& entry) const;

		void OfferFolder(const FileNode& folder, const std::vector<DirectoryEntry>& entries) const;

		FileNode UnloadedNodeFor(const FileNode& folder, const DirectoryEntry& entry) const;

		static void RebaseNode(FileNode& node, const std::filesystem::path& newPath);

		FileNode m_Root;
		bool m_HasRoot = false;
		IgnoreQuery m_IgnoreQuery;
		IgnoreFolderQuery m_IgnoreFolderQuery;
	};

	bool IsFileNodeListedBefore(const FileNode& left, const FileNode& right);
}