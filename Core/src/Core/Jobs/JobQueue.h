#pragma once

#include "Core/Base/NonCopyable.h"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace Core {

	class JobQueue : NonCopyable
	{
	public:
		static constexpr std::size_t kOneWorkerPerSpareCore = 0;

		explicit JobQueue(std::size_t requestedWorkers = kOneWorkerPerSpareCore);
		~JobQueue();

		void Submit(std::function<void()> job);

	private:
		void Run();
		void StopAndJoin();

		std::mutex m_Mutex;
		std::condition_variable m_Wake;
		std::deque<std::function<void()>> m_Jobs;
		bool m_Stopping = false;

		// Last, so the workers are started only once everything Run touches is built, and joined
		// before any of it is destroyed.
		std::vector<std::thread> m_Workers;
	};
}
