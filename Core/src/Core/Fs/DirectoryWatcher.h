#pragma once

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
#include "Core/Fs/ChangeCollector.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace Core {

	class DirectoryWatcher final : NonCopyable
	{
	public:
		static constexpr std::chrono::milliseconds kQuietPeriod{250};

		~DirectoryWatcher();

		static Result<std::unique_ptr<DirectoryWatcher>> Start(
			const std::filesystem::path& root);

		struct Changes
		{
			std::vector<std::filesystem::path> directories;

			bool overflowed = false;

			std::optional<std::filesystem::path> movedTo;
		};

		Changes TakeSettled();

		bool IsRunning() const { return m_Running.load(); }

	private:
		DirectoryWatcher() = default;

		void Run();

		std::filesystem::path QueryHandlePath() const;

		void CheckForMove();

		void CheckForMoveIfDue();
		void RecordChange(const std::filesystem::path& path);

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