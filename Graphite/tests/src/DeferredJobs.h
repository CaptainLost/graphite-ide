#pragma once

#include "Core/Jobs/JobSubmitter.h"

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

// Holds submitted jobs until the test runs them, as a busy worker would, so a test can look between a job being
// asked for and it running.
class DeferredJobs
{
public:
	Core::JobSubmitter Submitter()
	{
		return [this](std::function<void()> job) { m_Jobs.push_back(std::move(job)); };
	}

	// Runs what a job submits as well, in order.
	void RunAll()
	{
		while (!m_Jobs.empty())
		{
			std::vector<std::function<void()>> waiting;
			waiting.swap(m_Jobs);

			for (const std::function<void()>& job : waiting)
			{
				job();
			}
		}
	}

	// Runs the oldest job alone, as a worker taking it from the queue, and whatever it submits waits behind the rest.
	// False when no job waits.
	bool RunOldest()
	{
		if (m_Jobs.empty())
		{
			return false;
		}

		std::function<void()> job = std::move(m_Jobs.front());
		m_Jobs.erase(m_Jobs.begin());
		job();
		return true;
	}

	std::size_t Count() const { return m_Jobs.size(); }

private:
	std::vector<std::function<void()>> m_Jobs;
};
