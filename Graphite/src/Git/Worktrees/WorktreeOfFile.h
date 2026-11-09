#pragma once

#include "Core/Git/Worktree.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	// The deepest listed worktree holding `file`, unless that is the open folder's own; its folder name. Worktrees often
	// live inside the main checkout, so "inside the open folder" does not decide whose file it is. Empty for a file no
	// listed worktree holds.
	std::optional<std::string> WorktreeFolderOf(const std::filesystem::path& file,
												const std::vector<Core::WorktreeEntry>& entries,
												const std::filesystem::path& openFolder);

	// Whether a directory strictly between `workingDirectory` and the file at `relative` holds a `.git` entry, file or
	// folder: the file then belongs to that checkout, not to the repository of `workingDirectory`.
	// WARNING: Asks the disk once per directory below the working directory, so a caller asks it only for a file it is
	// about to answer for.
	bool IsInsideNestedCheckout(const std::filesystem::path& workingDirectory, const std::filesystem::path& relative);
}
