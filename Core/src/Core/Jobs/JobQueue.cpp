#include "corepch.h"

#include "Core/Jobs/JobQueue.h"

#include "Core/Base/Log.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace Core {

	namespace {

		constexpr std::size_t kCoresKeptForUserInterfaceThread = 1;

		// A queue with no workers accepts jobs and runs none, which looks exactly like a hang.
		constexpr std::size_t kFewestWorkers = 1;

		std::size_t WorkerCountFor(std::size_t requestedWorkers)
		{
			const bool workerCountGiven = requestedWorkers != JobQueue::kOneWorkerPerSpareCore;

			if (workerCountGiven)
			{
				return requestedWorkers;
			}

			// hardware_concurrency is allowed to return 0 when it cannot tell.
			const unsigned int cores = std::thread::hardware_concurrency();
			const bool spareCoresExceedFewestWorkers = cores > kCoresKeptForUserInterfaceThread + kFewestWorkers;

			return spareCoresExceedFewestWorkers ? static_cast<std::size_t>(cores) - kCoresKeptForUserInterfaceThread : kFewestWorkers;
		}
	}

	JobQueue::JobQueue(std::size_t requestedWorkers)
	{
		const std::size_t workerCount = WorkerCountFor(requestedWorkers);

		m_Workers.reserve(workerCount);

		// Starting a thread can throw std::system_error when the system is out of resources. The
		// destructor never runs for a constructor that throws, and a started std::thread destroyed
		// unjoined calls std::terminate, so the workers started so far are stopped and joined here
		// before the exception goes on.
		try
		{
			for (std::size_t workerIndex = 0; workerIndex < workerCount; ++workerIndex)
			{
				m_Workers.emplace_back([this] { Run(); });
			}
		}
		catch (...)
		{
			StopAndJoin();
			throw;
		}
	}

	JobQueue::~JobQueue()
	{
		StopAndJoin();
	}

	void JobQueue::StopAndJoin()
	{
		{
			const std::lock_guard<std::mutex> lock(m_Mutex);
			m_Stopping = true;
		}

		m_Wake.notify_all();

		for (std::thread& worker : m_Workers)
		{
			worker.join();
		}
	}

	void JobQueue::Submit(std::function<void()> job)
	{
		{
			const std::lock_guard<std::mutex> lock(m_Mutex);

			if (m_Stopping)
			{
				return;
			}

			m_Jobs.push_back(std::move(job));
		}

		m_Wake.notify_one();
	}

	void JobQueue::Run()
	{
		while (true)
		{
			std::function<void()> job;

			{
				std::unique_lock<std::mutex> lock(m_Mutex);

				m_Wake.wait(lock, [this] { return m_Stopping || !m_Jobs.empty(); });

				const bool stoppingWithQueueDrained = m_Jobs.empty();

				if (stoppingWithQueueDrained)
				{
					return;
				}

				job = std::move(m_Jobs.front());
				m_Jobs.pop_front();
			}

			// An exception leaving a thread function calls std::terminate, so an allocation
			// failure or a path conversion deep inside one job would take the whole process
			// down with no message.
			try
			{
				job();
			}
			catch (const std::exception& error)
			{
				CORE_ERROR("A job threw and was abandoned: {}", error.what());
			}
			catch (...)
			{
				CORE_ERROR("A job threw something that is not an exception and was abandoned");
			}
		}
	}
}
