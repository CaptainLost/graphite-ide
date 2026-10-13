#include "Review/WorktreeWatch.h"

#include "Core/Base/Log.h"
#include "Core/FileSystem/DirectoryWatcher.h"
#include "Core/FileSystem/FileSystem.h"

#include <utility>

namespace Graphite {

	namespace {

		class DirectoryWorktreeWatch final : public WorktreeWatch
		{
		public:
			explicit DirectoryWorktreeWatch(Core::Scope<Core::DirectoryWatcher> watcher)
				: m_Watcher(std::move(watcher))
			{
			}

			bool TakeChanged() override
			{
				const Core::DirectoryWatcher::Changes changes = m_Watcher->TakeSettled();

				return !changes.Directories.empty() || changes.Overflowed || changes.RepositoryStateChanged ||
					   changes.MovedTo.has_value();
			}

		private:
			Core::Scope<Core::DirectoryWatcher> m_Watcher;
		};

		Core::Scope<WorktreeWatch> StartDirectoryWatch(const std::filesystem::path& worktree)
		{
			Core::Result<Core::Scope<Core::DirectoryWatcher>> watcher = Core::DirectoryWatcher::Start(worktree);

			if (!watcher.HasValue())
			{
				APP_WARN("Cannot watch the worktree at {}: {}", Core::ToUtf8(worktree), watcher.GetError().Message);
				return nullptr;
			}

			return Core::CreateScope<DirectoryWorktreeWatch>(std::move(watcher).GetValue());
		}
	}

	WorktreeWatchFactory WatchWithDirectoryWatcher()
	{
		return StartDirectoryWatch;
	}
}
