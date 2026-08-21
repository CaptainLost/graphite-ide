#pragma once

#include "Core/Base/Base.h"

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
#include "Core/Fs/ChangeCollector.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace Core {

	// Whether a path, relative to the watched root, is one of the few files that say what a
	// repository is rather than what it holds: HEAD, the index, packed-refs, and everything
	// under refs. Those change exactly when the answer to "what is in this repository"
	// changes, which is what makes them worth hearing about when the rest of .git - objects
	// rewritten on every operation, lock files, edit messages, reflogs - is not.
	//
	// Only inside the repository's own directory. A project with its own file called HEAD
	// has an ordinary file, and editing it is an edit to the working tree like any other.
	bool NamesRepositoryState(const std::filesystem::path& relative);

	class DirectoryWatcher final : NonCopyable
	{
	public:
		static constexpr std::chrono::milliseconds kQuietPeriod{250};

		~DirectoryWatcher();

		// Returns only once the watch is actually armed. Windows buffers nothing for a
		// directory handle until a read has been issued against it, and that read happens on
		// the worker thread, so returning as soon as the handle existed would drop every
		// change made before the worker was first scheduled.
		static Result<Scope<DirectoryWatcher>> Start(
			const std::filesystem::path& root);

		struct Changes
		{
			std::vector<std::filesystem::path> Directories;

			bool Overflowed = false;

			// Something a commit, a checkout, a fetch or a rebase rewrote - see
			// NamesRepositoryState. Carried apart from Directories because the working tree
			// did not change: a branch moving is nothing for the file tree to redraw, and
			// everything for whatever draws the history.
			bool RepositoryState = false;

			std::optional<std::filesystem::path> MovedTo;
		};

		Changes TakeSettled();

		bool IsRunning() const { return m_Running.load(); }

	private:
		DirectoryWatcher() = default;

		void Run();

		std::filesystem::path QueryHandlePath() const;

		void CheckForMove();

		// Set by the watcher thread, taken by TakeSettled. Under the same mutex as the
		// collector beside it: they describe the same batch, and a take that read one
		// without the other could report a repository change in a batch that no longer
		// carries it.
		bool m_RepositoryState = false;

		void CheckForMoveIfDue();
		void RecordChange(const std::filesystem::path& path);

		void RecordChangesIn(const void* records, std::size_t bytes);

		void RecordOverflow();

		std::filesystem::path m_Root;
		std::thread m_Thread;
		std::mutex m_Mutex; // guards m_Collector and m_MovedTo
		ChangeCollector m_Collector;

		std::atomic<bool> m_Running{true};

		std::promise<Result<void>> m_Armed;

		static constexpr std::chrono::milliseconds kMoveCheckInterval{2000};

		std::filesystem::path m_WatchedPath;

		ChangeCollector::Clock::time_point m_LastMoveCheck;

		std::optional<std::filesystem::path> m_MovedTo;

		void* m_Directory = nullptr;
		void* m_StopEvent = nullptr;
	};
}