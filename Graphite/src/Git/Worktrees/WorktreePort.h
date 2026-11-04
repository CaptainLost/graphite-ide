#pragma once

#include "Core/Base/Result.h"
#include "Core/Git/Worktree.h"

#include <cstddef>
#include <filesystem>
#include <vector>

namespace Graphite {

	// The worktrees of the open folder's repository.
	class WorktreePort
	{
	public:
		virtual ~WorktreePort() = default;

		virtual Core::Result<std::vector<Core::WorktreeEntry>> QueryWorktrees() = 0;

		// The files with uncommitted changes in the worktree at that folder, a wholly untracked folder counted as one.
		virtual Core::Result<std::size_t> CountUncommitted(const std::filesystem::path& worktree) = 0;
	};
}
