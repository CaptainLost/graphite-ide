#include "Git/Worktrees/WorktreeOfFile.h"

#include "Core/FileSystem/FileSystem.h"

#include <system_error>

namespace Graphite {

	namespace {
		std::size_t DepthOf(const std::filesystem::path& folder)
		{
			std::size_t depth = 0;

			for (const std::filesystem::path& part : Core::NormalizeDirectory(folder))
			{
				if (!part.empty())
				{
					++depth;
				}
			}

			return depth;
		}

		std::string FolderNameOf(const std::filesystem::path& folder)
		{
			const std::filesystem::path normal = Core::NormalizeDirectory(folder);
			return Core::ToUtf8(normal.has_filename() ? normal.filename() : normal.parent_path().filename());
		}
	}

	std::optional<std::string> WorktreeFolderOf(const std::filesystem::path& file,
												const std::vector<Core::WorktreeEntry>& entries,
												const std::filesystem::path& openFolder)
	{
		const Core::WorktreeEntry* deepest = nullptr;
		std::size_t deepestDepth = 0;

		for (const Core::WorktreeEntry& entry : entries)
		{
			if (entry.Path.empty() || !Core::IsSameOrWithin(file, entry.Path))
			{
				continue;
			}

			const std::size_t depth = DepthOf(entry.Path);

			if (deepest == nullptr || depth > deepestDepth)
			{
				deepest = &entry;
				deepestDepth = depth;
			}
		}

		if (deepest == nullptr || Core::SamePath(deepest->Path, openFolder))
		{
			return std::nullopt;
		}

		return FolderNameOf(deepest->Path);
	}

	std::optional<std::string> WorktreeNameCache::Of(const std::filesystem::path& file, std::uint64_t generation,
													 const std::vector<Core::WorktreeEntry>& entries,
													 const std::filesystem::path& openFolder)
	{
		if (generation != m_Generation || openFolder != m_OpenFolder)
		{
			m_Names.clear();
			m_Generation = generation;
			m_OpenFolder = openFolder;
		}

		const auto found = m_Names.find(file.native());

		if (found != m_Names.end())
		{
			return found->second;
		}

		return m_Names.emplace(file.native(), WorktreeFolderOf(file, entries, openFolder)).first->second;
	}

	bool IsInsideNestedCheckout(const std::filesystem::path& workingDirectory, const std::filesystem::path& relative)
	{
		std::filesystem::path directory = workingDirectory;
		const std::filesystem::path parents = relative.parent_path();

		for (const std::filesystem::path& part : parents)
		{
			if (part.empty() || part == ".")
			{
				continue;
			}

			directory /= part;

			std::error_code error;

			if (std::filesystem::exists(directory / ".git", error))
			{
				return true;
			}
		}

		return false;
	}
}
