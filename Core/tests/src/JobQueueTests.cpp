#include <doctest/doctest.h>

#include "Core/Jobs/CancellationToken.h"
#include "Core/Jobs/JobQueue.h"

#include <atomic>
#include <chrono>
#include <thread>

namespace {

	// Waits for a condition rather than sleeping a fixed time: a fixed sleep is either flaky on
	// a loaded machine or slow on an idle one. Fails by timing out, so a broken queue makes the
	// test fail rather than hang the suite.
	bool WaitFor(const std::function<bool()>& done)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

		while (std::chrono::steady_clock::now() < deadline)
		{
			if (done())
			{
				return true;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		return false;
	}
}

TEST_CASE("JobQueue runs what it is given")
{
	std::atomic_int done{0};

	{
		Core::JobQueue queue(2);

		for (int job = 0; job < 50; ++job)
		{
			queue.Submit([&done] { done.fetch_add(1); });
		}

		CHECK(WaitFor([&done] { return done.load() == 50; }));
	}

	CHECK(done.load() == 50);
}

// The destructor must join. If it detached instead, the counter could still be climbing after
// the queue is gone — and the job's captures with it.
TEST_CASE("JobQueue finishes its queue before it is destroyed")
{
	std::atomic_int done{0};

	{
		Core::JobQueue queue(1);

		for (int job = 0; job < 20; ++job)
		{
			queue.Submit([&done]
						 {
							 std::this_thread::sleep_for(std::chrono::milliseconds(1));
							 done.fetch_add(1);
						 });
		}
	}

	CHECK(done.load() == 20);
}

TEST_CASE("A cancelled token stops a cooperative job")
{
	const Core::CancellationToken token;

	std::atomic_int rounds{0};
	std::atomic_bool finished{false};

	{
		Core::JobQueue queue(1);

		queue.Submit([&]
					 {
						 while (!token.IsCancelled())
						 {
							 rounds.fetch_add(1);
							 std::this_thread::sleep_for(std::chrono::milliseconds(1));
						 }

						 finished.store(true);
					 });

		CHECK(WaitFor([&rounds] { return rounds.load() > 0; }));

		token.Cancel();

		CHECK(WaitFor([&finished] { return finished.load(); }));
	}

	CHECK(finished.load());
}

TEST_CASE("A token copy shares the flag with its original")
{
	const Core::CancellationToken token;
	const Core::CancellationToken copy = token;

	CHECK_FALSE(copy.IsCancelled());

	token.Cancel();

	CHECK(copy.IsCancelled());
}
