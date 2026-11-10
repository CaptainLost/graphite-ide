#pragma once

#include "Core/Base/Result.h"
#include "Core/FileSystem/FolderWatch.h"

#include "Core/Base/Base.h"

#include <filesystem>
#include <functional>

namespace Graphite {

	class WorktreeWatch
	{
	public:
		virtual ~WorktreeWatch() = default;

		virtual bool TakeChanged() = 0;
	};

	using WorktreeWatchFactory = std::function<Core::Scope<WorktreeWatch>(const std::filesystem::path& worktree)>;

	// Watches through whatever start gives: the Windows watcher for a local folder, the distro's agent for one inside WSL.
	// A watch that stops for good is started again, at most every few seconds, and reports a change when it is.
	WorktreeWatchFactory WatchWithFolderWatch(std::function<Core::Result<Core::Scope<Core::FolderWatch>>(const std::filesystem::path& folder)> start);

}
