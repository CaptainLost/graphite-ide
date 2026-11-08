#pragma once

#include "Core/Base/Result.h"
#include "Core/Git/Worktree.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

namespace Graphite {

	// When a worktree last did something, from three cheap reads that walk no status. Each is unset when its source is
	// missing: no reflog yet, no index yet, an unborn HEAD.
	struct WorktreeActivity
	{
		// <gitdir>/logs/HEAD modification time.
		std::optional<std::chrono::sys_seconds> Reflog;

		// <gitdir>/index modification time.
		std::optional<std::chrono::sys_seconds> Index;

		// The committer time of the commit HEAD names.
		std::optional<std::chrono::sys_seconds> HeadCommit;
	};

	struct WorktreeCount
	{
		std::size_t Changed = 0;

		// The newest modification time among the changed files that still exist; a deleted file has none.
		std::optional<std::chrono::sys_seconds> NewestChange;
	};

	// The worktrees of the open folder's repository.
	class WorktreePort
	{
	public:
		virtual ~WorktreePort() = default;

		virtual Core::Result<std::vector<Core::WorktreeEntry>> QueryWorktrees() = 0;

		virtual Core::Result<WorktreeActivity> QueryActivity(const std::filesystem::path& worktree) = 0;

		// The files with uncommitted changes in the worktree at that folder, a wholly untracked folder counted as one,
		// and a submodule not looked into.
		virtual Core::Result<WorktreeCount> CountUncommitted(const std::filesystem::path& worktree) = 0;
	};
}
