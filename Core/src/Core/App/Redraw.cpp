#include "corepch.h"

#include "Core/App/Redraw.h"

#include <atomic>
#include <mutex>

namespace Core {

	namespace {

		// Guarded rather than atomic because of the one caller that can outlive the window: a
		// system dialog's detached thread. With a bare atomic it could load the waker, be
		// preempted while the loop clears it and GLFW terminates, then post into a torn-down
		// GLFW. Holding the lock across the call makes SetWaker(nullptr) wait for it.
		std::mutex s_WakerMutex;
		Redraw::Waker s_Waker = nullptr;

		std::atomic_bool s_Requested{false};

		constexpr SteadyClock::rep kNoDeadline = SteadyClock::duration::max().count();
		std::atomic<SteadyClock::rep> s_DeadlineTicksSinceEpoch{kNoDeadline};
	}

	void Redraw::Request()
	{
		// The flag goes up before the wake: the loop takes the flag after its wait returns, and a
		// wake that landed first would find nothing to take. Only the request that raises the
		// flag wakes, because a wake per request would put a message per request in the window's
		// queue, which Windows caps.
		const bool alreadyRequested = s_Requested.exchange(true);

		if (alreadyRequested)
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
		const SteadyClock::time_point wantedDeadline = SteadyClock::now() + std::chrono::duration_cast<SteadyClock::duration>(delay);
		const SteadyClock::rep wantedTicks = wantedDeadline.time_since_epoch().count();
		SteadyClock::rep currentTicks = s_DeadlineTicksSinceEpoch.load();

		// compare_exchange_weak reloads currentTicks when it fails, which it may also do
		// spuriously, so this retries until the earlier deadline is stored or one at least as
		// early already is.
		while (wantedTicks < currentTicks)
		{
			const bool stored = s_DeadlineTicksSinceEpoch.compare_exchange_weak(currentTicks, wantedTicks);

			if (stored)
			{
				break;
			}
		}
	}

	void Redraw::SetWaker(Waker wakeLoopFromAnyThread)
	{
		const std::lock_guard lock{s_WakerMutex};
		s_Waker = wakeLoopFromAnyThread;
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
		const SteadyClock::rep deadlineTicks = s_DeadlineTicksSinceEpoch.exchange(kNoDeadline);

		if (deadlineTicks == kNoDeadline)
		{
			return std::nullopt;
		}

		return SteadyClock::time_point{SteadyClock::duration{deadlineTicks}};
	}
}
