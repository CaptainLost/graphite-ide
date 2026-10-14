#pragma once

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

	WorktreeWatchFactory WatchWithDirectoryWatcher();
}
