#pragma once

#include "Core/Git/Worktree.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Graphite {

	// The deepest listed worktree holding `file`, unless that is the open folder's own; its folder name. Worktrees often
	// live inside the main checkout, so "inside the open folder" does not decide whose file it is. Empty for a file no
	// listed worktree holds.
	std::optional<std::string> WorktreeFolderOf(const std::filesystem::path& file,
												const std::vector<Core::WorktreeEntry>& entries,
												const std::filesystem::path& openFolder);

	// WorktreeFolderOf answered once per file for as long as the worktree list and the folder stay as they were, so that
	// a frame that changes neither does no path comparison. UI thread only.
	class WorktreeNameCache
	{
	public:
		// `openFolder` is the repository's working directory, never a subfolder of it: the open folder's own entry is the
		// one WorktreeFolderOf leaves unnamed. `generation` changes whenever `entries` does.
		std::optional<std::string> Of(const std::filesystem::path& file, std::uint64_t generation,
									  const std::vector<Core::WorktreeEntry>& entries,
									  const std::filesystem::path& openFolder);

	private:
		std::uint64_t m_Generation = 0;
		std::filesystem::path m_OpenFolder;
		std::unordered_map<std::filesystem::path::string_type, std::optional<std::string>> m_Names;
	};

	// Whether a directory strictly between `workingDirectory` and the file at `relative` holds a `.git` entry, file or
	// folder: the file then belongs to that checkout, not to the repository of `workingDirectory`.
	// WARNING: Asks the disk once per directory below the working directory, so a caller asks it only for a file it is
	// about to answer for.
	bool IsInsideNestedCheckout(const std::filesystem::path& workingDirectory, const std::filesystem::path& relative);
}
