#include "Review/WorktreeWatch.h"

#include "Core/Base/Log.h"
#include "Core/FileSystem/DirectoryWatcher.h"
#include "Core/FileSystem/FileSystem.h"

#include <utility>

namespace Graphite {

	namespace {

		class FolderWorktreeWatch final : public WorktreeWatch
		{
		public:
			explicit FolderWorktreeWatch(Core::Scope<Core::FolderWatch> watch) : m_Watch(std::move(watch)) {}

			bool TakeChanged() override
			{
				const Core::FolderChanges changes = m_Watch->TakeSettled();

				return !changes.Directories.empty() || changes.Overflowed || changes.RepositoryStateChanged || changes.MovedTo.has_value();
			}

		private:
			Core::Scope<Core::FolderWatch> m_Watch;
		};
	}

	WorktreeWatchFactory WatchWithFolderWatch(std::function<Core::Result<Core::Scope<Core::FolderWatch>>(const std::filesystem::path& folder)> start)
	{
		return [start = std::move(start)](const std::filesystem::path& worktree) -> Core::Scope<WorktreeWatch> {
			Core::Result<Core::Scope<Core::FolderWatch>> watch = start(worktree);

			if (!watch.HasValue())
			{
				APP_WARN("Cannot watch the worktree at {}: {}", Core::ToUtf8(worktree), watch.GetError().Message);
				return nullptr;
			}

			return Core::CreateScope<FolderWorktreeWatch>(std::move(watch).GetValue());
		};
	}

	WorktreeWatchFactory WatchWithDirectoryWatcher()
	{
		return WatchWithFolderWatch(Core::WatchDirectory);
	}
}
