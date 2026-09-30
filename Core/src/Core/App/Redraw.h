#pragma once

#include <chrono>
#include <optional>

// How the rest of the program tells a sleeping frame loop that it has something to show.
//
// The loop draws only while there is reason to (see FramePacer) and otherwise sleeps until
// an event arrives. Anything that changes what the screen should show without an input
// event — a worker finishing, a reader thread receiving output, a timer coming due — has to
// say so here, or it is only seen on the loop's idle tick.
//
// Process-wide rather than handed down: the producers are worker threads several layers
// below anything that knows about the window, and a waker threaded through every one of
// them would be plumbing for a single call.
namespace Core::Redraw {

	using Clock = std::chrono::steady_clock;

	// Any thread. Wakes the loop for a few frames — enough to drain the result and for ImGui
	// to settle whatever it changed, not a second of them: a process writing into a watched
	// folder would otherwise keep the loop drawing at the refresh rate. Whatever the result
	// sets off later (a debounce, a settle period) asks for its own frame with RequestIn.
	// Cheap enough to call per result: requests made before the loop takes the first share
	// its one wake.
	void Request();

	// The UI thread. Asks for a frame no later than `delay` from now, without the frames that
	// follow a Request: a blinking caret wants one frame per phase, not the loop kept awake.
	// Read when the loop begins to wait, so a call from another thread during a wait takes
	// effect only at the next one. A request lasts for one wait; a frame that still wants
	// one asks again.
	void RequestIn(std::chrono::duration<double> delay);

	// The loop's side. The waker is whatever interrupts the loop's wait from another thread;
	// until one is set — in tests, or before the window exists — Request only records. Once
	// SetWaker returns, no call to the previous waker is still running.
	void SetWaker(void (*waker)());

	// Whether a Request is waiting to be taken, without taking it.
	bool IsRequested();

	// Whether Request was called since the last take, clearing it.
	bool TakeRequest();

	// The earliest RequestIn deadline since the last take, clearing it.
	std::optional<Clock::time_point> TakeDeadline();
}
