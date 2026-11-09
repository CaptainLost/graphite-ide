#pragma once

#include "Core/Base/Base.h"

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
#include "Core/FileSystem/ChangeBatcher.h"
#include "Core/Platform/UniqueHandle.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <thread>
#include <vector>

namespace Core {

	class DirectoryWatcher final : NonCopyable
	{
	public:
		static constexpr std::chrono::milliseconds kQuietPeriod{250};

		~DirectoryWatcher();

		// Returns only once the watch is armed. Windows records nothing for a directory handle
		// until a read has been issued against it, and that read is issued on the worker thread,
		// so returning as soon as the handle exists would lose every change made before then.
		static Result<Scope<DirectoryWatcher>> Start(
			const std::filesystem::path& root);

		struct Changes
		{
			std::vector<std::filesystem::path> Directories;

			bool Overflowed = false;

			bool IgnoreRulesChanged = false;

			bool RepositoryStateChanged = false;

			std::optional<std::filesystem::path> MovedTo;
		};

		Changes TakeSettled();

		bool IsRunning() const { return m_Running.load(); }

	private:
		DirectoryWatcher() = default;

		void Run();

		std::filesystem::path QueryHandlePath() const;

		void CheckForMove();
		void CheckForMoveIfDue();
		void RecordChange(const std::filesystem::path& changedPath);
		void RecordChangesIn(std::span<const std::byte> filledNotificationRecords);
		void RecordOverflow();

		static constexpr std::chrono::milliseconds kMoveCheckInterval{2000};

		std::filesystem::path m_Root;
		std::thread m_Thread;

		// Guards m_Batcher and m_MovedTo, which together make one batch: a take that read one of
		// them without the other could split the batch.
		std::mutex m_Mutex;
		ChangeBatcher m_Batcher{std::filesystem::path{}};

		std::atomic<bool> m_Running{true};

		std::promise<Result<void>> m_Armed;

		std::filesystem::path m_WatchedPath;

		ChangeCollector::Clock::time_point m_LastMoveCheck;

		std::optional<std::filesystem::path> m_MovedTo;

		UniqueHandle m_Directory;
		UniqueHandle m_StopEvent;
	};
}
