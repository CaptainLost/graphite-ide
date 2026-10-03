#include <doctest/doctest.h>

#include "Core/Jobs/CancellationToken.h"
#include "Core/Jobs/JobQueue.h"

#include <atomic>
#include <chrono>
#include <stdexcept>
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
			queue.Submit([&done] {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				done.fetch_add(1);
			});
		}
	}

	CHECK(done.load() == 20);
}

// One worker on purpose: the job that follows has to run on the very thread the exception
// escaped from, so a worker that died with it would leave `after` false forever.
TEST_CASE("A job that throws does not take its worker with it")
{
	std::atomic_bool after{false};

	{
		Core::JobQueue queue(1);

		queue.Submit([] { throw std::runtime_error("a job that fails"); });
		queue.Submit([&after] { after.store(true); });

		CHECK(WaitFor([&after] { return after.load(); }));
	}

	CHECK(after.load());
}

TEST_CASE("A cancelled token stops a cooperative job")
{
	const Core::CancellationToken token;

	std::atomic_int rounds{0};
	std::atomic_bool finished{false};

	{
		Core::JobQueue queue(1);

		queue.Submit([&] {
			// Bounded independently of the token: if cancellation ever stops
			// propagating, this test must fail, and a job that can only be stopped
			// by the thing under test would instead hang the destructor's join.
			for (int round = 0; round < 100000 && !token.IsCancelled(); ++round)
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

	// finished alone is true on both exit paths (cancelled or bound exhausted). Comparing
	// rounds against the bound is what actually proves the loop stopped because it saw the
	// cancellation, not because it ran out of iterations.
	CHECK(rounds.load() < 100000);
}

TEST_CASE("A token copy shares the flag with its original")
{
	const Core::CancellationToken token;
	const Core::CancellationToken copy = token;

	CHECK_FALSE(copy.IsCancelled());

	token.Cancel();

	CHECK(copy.IsCancelled());
}
