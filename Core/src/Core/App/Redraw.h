#pragma once

#include "Core/Base/Timestep.h"

#include <chrono>
#include <optional>

namespace Core {

	class Redraw final
	{
	public:
		Redraw() = delete;

		using Clock = SteadyClock;
		using Waker = void (*)();

		// Any thread. Whatever changes what the screen should show without an input event has to
		// call this, or the change is seen only on the loop's idle tick. Whatever the result sets
		// off later, such as a debounce or a settle period, asks for its own frame with RequestIn.
		static void Request();

		// The UI thread: the deadline is read when the loop begins to wait, so a call from another
		// thread during a wait takes effect only at the next one. A deadline lasts for one wait; a
		// frame that still wants one asks again.
		static void RequestIn(std::chrono::duration<double> delay);

		// Once SetWaker returns, no call to the previous waker is still running.
		static void SetWaker(Waker wakeLoopFromAnyThread);

		static bool IsRequested();
		static bool TakeRequest();
		static std::optional<Clock::time_point> TakeDeadline();
	};
}
