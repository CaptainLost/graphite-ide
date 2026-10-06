#include <doctest/doctest.h>

#include "Core/Jobs/CancellationToken.h"
#include "Core/Jobs/JobQueue.h"

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace {

	constexpr std::chrono::seconds kGiveUpAfter{5};
	constexpr std::chrono::milliseconds kPollInterval{1};
	constexpr std::chrono::milliseconds kSimulatedWork{1};

	constexpr std::size_t kSingleWorker = 1;
	constexpr std::size_t kTwoWorkers = 2;

	constexpr int kJobsForTwoWorkers = 50;
	constexpr int kJobsQueuedBeforeDestruction = 20;

	// Bounded independently of the token: if cancellation ever stops propagating, this test must
	// fail, and a job that can only be stopped by the thing under test would instead hang the
	// destructor's join.
	constexpr int kRoundsBeforeGivingUp = 100000;

	// Polls rather than sleeping a fixed time: a fixed sleep is either flaky on a loaded machine
	// or slow on an idle one.
	bool WaitUntilTrueOrTimeOut(const std::function<bool()>& condition)
	{
		const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kGiveUpAfter;

		while (std::chrono::steady_clock::now() < deadline)
		{
			if (condition())
			{
				return true;
			}

			std::this_thread::sleep_for(kPollInterval);
		}

		return false;
	}

	void CountRoundsUntilCancelled(const Core::CancellationToken& token, std::atomic_int& roundsRun, std::atomic_bool& jobFinished)
	{
		for (int round = 0; round < kRoundsBeforeGivingUp && !token.IsCancelled(); ++round)
		{
			roundsRun.fetch_add(1);
			std::this_thread::sleep_for(kSimulatedWork);
		}

		jobFinished.store(true);
	}
}

TEST_CASE("JobQueue runs what it is given")
{
	std::atomic_int jobsRun{0};

	{
		Core::JobQueue queue(kTwoWorkers);

		for (int jobIndex = 0; jobIndex < kJobsForTwoWorkers; ++jobIndex)
		{
			queue.Submit([&jobsRun] { jobsRun.fetch_add(1); });
		}

		CHECK(WaitUntilTrueOrTimeOut([&jobsRun] { return jobsRun.load() == kJobsForTwoWorkers; }));
	}

	CHECK(jobsRun.load() == kJobsForTwoWorkers);
}

TEST_CASE("JobQueue finishes its queue before it is destroyed so no job outlives its captures")
{
	std::atomic_int jobsRun{0};

	{
		Core::JobQueue queue(kSingleWorker);

		for (int jobIndex = 0; jobIndex < kJobsQueuedBeforeDestruction; ++jobIndex)
		{
			queue.Submit([&jobsRun] {
				std::this_thread::sleep_for(kSimulatedWork);
				jobsRun.fetch_add(1);
			});
		}
	}

	CHECK(jobsRun.load() == kJobsQueuedBeforeDestruction);
}

TEST_CASE("A job that throws does not take its worker with it so the next job on a single-worker queue still runs")
{
	std::atomic_bool nextJobRan{false};

	{
		Core::JobQueue queue(kSingleWorker);

		queue.Submit([] { throw std::runtime_error("a job that fails"); });
		queue.Submit([&nextJobRan] { nextJobRan.store(true); });

		CHECK(WaitUntilTrueOrTimeOut([&nextJobRan] { return nextJobRan.load(); }));
	}

	CHECK(nextJobRan.load());
}

TEST_CASE("A cancelled token stops a cooperative job before it runs out of rounds")
{
	const Core::CancellationToken token;

	std::atomic_int roundsRun{0};
	std::atomic_bool jobFinished{false};

	{
		Core::JobQueue queue(kSingleWorker);

		queue.Submit([&token, &roundsRun, &jobFinished] { CountRoundsUntilCancelled(token, roundsRun, jobFinished); });

		CHECK(WaitUntilTrueOrTimeOut([&roundsRun] { return roundsRun.load() > 0; }));

		token.Cancel();

		CHECK(WaitUntilTrueOrTimeOut([&jobFinished] { return jobFinished.load(); }));
	}

	CHECK(jobFinished.load());
	CHECK(roundsRun.load() < kRoundsBeforeGivingUp);
}

TEST_CASE("A token copy shares the flag with its original so the thread that starts a job and the thread that runs it hold one token")
{
	const Core::CancellationToken token;
	const Core::CancellationToken copy = token;

	CHECK_FALSE(copy.IsCancelled());

	token.Cancel();

	CHECK(copy.IsCancelled());
}
