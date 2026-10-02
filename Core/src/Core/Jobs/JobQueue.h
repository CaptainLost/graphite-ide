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

	// A fixed set of worker threads pulling from one queue. Deliberately the smallest thing that
	// works: no priorities, no work stealing, no dependencies between jobs. The stage that needs
	// those will know what shape they should take; guessing now would fix the wrong shape.
	class JobQueue : NonCopyable
	{
	public:
		// Zero asks the hardware, leaving one core for the UI thread, and never yields fewer
		// than one worker — a queue with no workers accepts jobs and runs none, which looks
		// exactly like a hang.
		explicit JobQueue(std::size_t workers = 0);

		// Stops accepting work, wakes every worker and joins. Jobs already running are waited
		// for: a worker that outlived this object would be running a function whose captures
		// are gone.
		~JobQueue();

		void Submit(std::function<void()> job);

	private:
		void Run();

		// What the destructor does, shared with a constructor that fails part-way: the workers
		// already started are joinable, and destroying one unjoined calls std::terminate.
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
