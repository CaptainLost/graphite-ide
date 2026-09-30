#pragma once

#include <chrono>
#include <optional>

namespace Core {

	// Decides how long the frame loop may sleep before drawing again.
	//
	// After input the loop draws every refresh for InputWindow. That covers what plays out
	// shortly after the user did something, without an event of its own: ImGui's tooltip and
	// hover delays, a scroll or focus request taking effect on the next frame, the commit
	// filter's settle. A Redraw::Request from a worker buys only RequestFrames: results arrive
	// without the user, sometimes several a second for as long as another process writes into
	// a watched folder, and a second of frames for each would keep the loop drawing
	// throughout. What a result sets off later asks for a deadline instead. Past both the loop
	// sleeps until the earliest deadline, but never longer than IdleTick, so a timer nobody
	// asked a deadline for is at most that late instead of never seen.
	class FramePacer
	{
	public:
		using Clock = std::chrono::steady_clock;

		static constexpr Clock::duration InputWindow = std::chrono::seconds(1);
		static constexpr Clock::duration IdleTick = std::chrono::seconds(1);

		// One to drain the result, and two more for what ImGui settles a frame late — a
		// window fitting new content, a table sizing a new column.
		static constexpr int RequestFrames = 3;

		void NoteInput(Clock::time_point now);
		void NoteRequest();

		// Called after every frame drawn, to count down what a request bought.
		void FrameDrawn();

		// Zero means draw the next frame straight away. A held pointer keeps the loop drawing
		// however long it stays still: a drag past the edge of a view scrolls it every frame.
		Clock::duration SleepFor(Clock::time_point now, bool pointerHeld,
								 std::optional<Clock::time_point> deadline) const;

	private:
		Clock::time_point m_InputUntil{};
		int m_FramesOwed = 0;
	};
}
