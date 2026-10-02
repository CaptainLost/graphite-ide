#include "corepch.h"

#include "Core/Jobs/JobQueue.h"

#include "Core/Base/Log.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace Core {

	namespace {

		std::size_t WorkerCount(std::size_t requested)
		{
			if (requested > 0)
			{
				return requested;
			}

			const unsigned int cores = std::thread::hardware_concurrency();

			// hardware_concurrency is allowed to return 0 when it cannot tell.
			return cores > 2 ? static_cast<std::size_t>(cores) - 1 : 1;
		}
	}

	JobQueue::JobQueue(std::size_t workers)
	{
		const std::size_t count = WorkerCount(workers);

		m_Workers.reserve(count);

		// Starting a thread can throw std::system_error when the system is out of resources. The
		// destructor never runs for a constructor that throws, so the workers started so far are
		// stopped and joined here before the exception goes on.
		try
		{
			for (std::size_t worker = 0; worker < count; ++worker)
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

				// Queued work is finished before stopping, so a job submitted a moment before
				// shutdown is not silently dropped.
				if (m_Jobs.empty())
				{
					return;
				}

				job = std::move(m_Jobs.front());
				m_Jobs.pop_front();
			}

			// An exception leaving a thread function calls std::terminate, so an allocation
			// failure or a path conversion deep inside one job would take the whole process
			// down with no message. The job is lost either way; the worker is not.
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
