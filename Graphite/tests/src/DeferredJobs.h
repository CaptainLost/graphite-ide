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

	std::size_t Count() const { return m_Jobs.size(); }

private:
	std::vector<std::function<void()>> m_Jobs;
};
