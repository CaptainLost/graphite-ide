#include "Review/WorktreeWatch.h"

#include "Core/Base/Log.h"
#include "Core/FileSystem/DirectoryWatcher.h"
#include "Core/FileSystem/FileSystem.h"

#include <chrono>
#include <utility>

namespace Graphite {

	namespace {

		using FolderWatchStart = std::function<Core::Result<Core::Scope<Core::FolderWatch>>(const std::filesystem::path& folder)>;

		// A watch that stopped for good (a WSL distro's agent gone, say) is started again no sooner than this.
		constexpr std::chrono::seconds kRestartInterval{10};

		Core::Scope<Core::FolderWatch> StartWatch(const FolderWatchStart& start, const std::filesystem::path& worktree)
		{
			Core::Result<Core::Scope<Core::FolderWatch>> watch = start(worktree);

			if (!watch.HasValue())
			{
				APP_WARN("Cannot watch the worktree at {}: {}", Core::ToUtf8(worktree), watch.GetError().Message);
				return nullptr;
			}

			return std::move(watch).GetValue();
		}

		class FolderWorktreeWatch final : public WorktreeWatch
		{
		public:
			FolderWorktreeWatch(Core::Scope<Core::FolderWatch> watch, FolderWatchStart start, std::filesystem::path worktree)
				: m_Watch(std::move(watch)), m_Start(std::move(start)), m_Worktree(std::move(worktree))
			{
			}

			bool TakeChanged() override
			{
				// What changed while nobody watched is unknown: a restarted watch reports a change, so it is read again.
				if (m_Watch == nullptr || !m_Watch->IsRunning())
				{
					return Restart();
				}

				const Core::FolderChanges changes = m_Watch->TakeSettled();

				return !changes.Directories.empty() || changes.Overflowed || changes.RepositoryStateChanged || changes.MovedTo.has_value();
			}

		private:
			bool Restart()
			{
				const auto now = std::chrono::steady_clock::now();

				if (m_LastRestart && now - *m_LastRestart < kRestartInterval)
				{
					return false;
				}

				m_LastRestart = now;
				m_Watch = StartWatch(m_Start, m_Worktree);
				return true;
			}

			Core::Scope<Core::FolderWatch> m_Watch;
			FolderWatchStart m_Start;
			std::filesystem::path m_Worktree;
			std::optional<std::chrono::steady_clock::time_point> m_LastRestart;
		};
	}

	WorktreeWatchFactory WatchWithFolderWatch(FolderWatchStart start)
	{
		return [start = std::move(start)](const std::filesystem::path& worktree) -> Core::Scope<WorktreeWatch> {
			Core::Scope<Core::FolderWatch> watch = StartWatch(start, worktree);

			if (watch == nullptr)
			{
				return nullptr;
			}

			return Core::CreateScope<FolderWorktreeWatch>(std::move(watch), start, worktree);
		};
	}
}
