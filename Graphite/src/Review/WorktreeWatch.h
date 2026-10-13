#pragma once

#include "Core/Base/Base.h"

#include <filesystem>
#include <functional>

namespace Graphite {

	// What the review model watches a worktree's files through, so its tests can say when a file
	// changed without touching a disk.
	class WorktreeWatch
	{
	public:
		virtual ~WorktreeWatch() = default;

		// True when anything under the worktree changed since the last call.
		virtual bool TakeChanged() = 0;
	};

	// Null when the directory cannot be watched. The uncommitted part then refreshes only when a
	// branch moves or the reader retries.
	using WorktreeWatchFactory = std::function<Core::Scope<WorktreeWatch>(const std::filesystem::path& worktree)>;

	WorktreeWatchFactory WatchWithDirectoryWatcher();
}
