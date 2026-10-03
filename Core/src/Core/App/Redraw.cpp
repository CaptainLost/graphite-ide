#include "corepch.h"

#include "Core/App/Redraw.h"

#include <atomic>
#include <mutex>

namespace Core {

	namespace {

		using Clock = Redraw::Clock;

		// Guarded rather than atomic because of the one caller that can outlive the window: a
		// system dialog's detached thread. With a bare atomic it could load the waker, be
		// preempted while the loop clears it and GLFW terminates, then post into a torn-down
		// GLFW. Holding the lock across the call makes SetWaker(nullptr) wait for it.
		std::mutex s_WakerMutex;
		void (*s_Waker)() = nullptr;

		std::atomic_bool s_Requested{false};

		// Ticks since the clock's epoch; the maximum means no deadline is pending.
		constexpr Clock::rep kNoDeadline = Clock::duration::max().count();
		std::atomic<Clock::rep> s_Deadline{kNoDeadline};
	}

	void Redraw::Request()
	{
		// The flag before the wake: the loop takes the flag after its wait returns, and a wake
		// that landed first would find nothing to take. Only the request that raises the flag
		// wakes; the rest ride on its wake. A search reports after every file, and a wake per
		// report would be a message per report in the window's queue, which Windows caps.
		if (s_Requested.exchange(true))
		{
			return;
		}

		const std::lock_guard lock{s_WakerMutex};

		if (s_Waker != nullptr)
		{
			s_Waker();
		}
	}

	void Redraw::RequestIn(std::chrono::duration<double> delay)
	{
		const Clock::rep wanted =
			(Clock::now() + std::chrono::duration_cast<Clock::duration>(delay))
				.time_since_epoch()
				.count();
		Clock::rep current = s_Deadline.load();

		while (wanted < current && !s_Deadline.compare_exchange_weak(current, wanted))
		{
		}
	}

	void Redraw::SetWaker(void (*waker)())
	{
		const std::lock_guard lock{s_WakerMutex};
		s_Waker = waker;
	}

	bool Redraw::IsRequested()
	{
		return s_Requested.load();
	}

	bool Redraw::TakeRequest()
	{
		return s_Requested.exchange(false);
	}

	std::optional<Redraw::Clock::time_point> Redraw::TakeDeadline()
	{
		const Clock::rep deadline = s_Deadline.exchange(kNoDeadline);

		if (deadline == kNoDeadline)
		{
			return std::nullopt;
		}

		return Clock::time_point{Clock::duration{deadline}};
	}
}
