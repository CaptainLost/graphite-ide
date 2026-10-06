#include <doctest/doctest.h>

#include "Core/App/FramePacer.h"
#include "Core/App/Redraw.h"

#include <chrono>
#include <optional>

using namespace std::chrono_literals;

namespace {

	using Clock = Core::FramePacer::Clock;

	const Clock::time_point kStart = Clock::time_point{} + 1000s;

	constexpr bool kPointerReleased = false;
	constexpr bool kPointerHeld = true;

	const std::optional<Clock::time_point> kNoDeadline = std::nullopt;

	void DiscardAnyStandingRequest()
	{
		(void)Core::Redraw::TakeRequest();
	}

	void DiscardAnyPendingDeadline()
	{
		(void)Core::Redraw::TakeDeadline();
	}
}

TEST_CASE("FramePacer draws every refresh for a while after input")
{
	Core::FramePacer pacer;
	pacer.NoteInput(kStart);

	CHECK(pacer.SleepFor(kStart, kPointerReleased, kNoDeadline) == Clock::duration::zero());
	CHECK(pacer.SleepFor(kStart + Core::FramePacer::kInputWindow - 1ms, kPointerReleased, kNoDeadline) ==
		  Clock::duration::zero());
}

TEST_CASE("FramePacer sleeps for the idle tick once the input window has passed")
{
	Core::FramePacer pacer;
	pacer.NoteInput(kStart);

	const Clock::time_point later = kStart + Core::FramePacer::kInputWindow;

	CHECK(pacer.SleepFor(later, kPointerReleased, kNoDeadline) == Core::FramePacer::kIdleTick);
}

TEST_CASE("FramePacer with no input yet sleeps")
{
	const Core::FramePacer pacer;

	CHECK(pacer.SleepFor(kStart, kPointerReleased, kNoDeadline) == Core::FramePacer::kIdleTick);
}

TEST_CASE("FramePacer wakes for a deadline sooner than the idle tick")
{
	const Core::FramePacer pacer;

	CHECK(pacer.SleepFor(kStart, kPointerReleased, kStart + 300ms) == 300ms);
}

TEST_CASE("FramePacer does not sleep past the idle tick for a later deadline")
{
	const Core::FramePacer pacer;

	CHECK(pacer.SleepFor(kStart, kPointerReleased, kStart + 1h) == Core::FramePacer::kIdleTick);
}

TEST_CASE("FramePacer draws at once for a deadline already past")
{
	const Core::FramePacer pacer;

	CHECK(pacer.SleepFor(kStart, kPointerReleased, kStart - 5s) == Clock::duration::zero());
}

TEST_CASE("FramePacer keeps drawing while the pointer is held so a drag past the edge of a view keeps scrolling")
{
	const Core::FramePacer pacer;

	CHECK(pacer.SleepFor(kStart, kPointerHeld, kNoDeadline) == Clock::duration::zero());
}

TEST_CASE("FramePacer input never shortens a window already running")
{
	Core::FramePacer pacer;
	pacer.NoteInput(kStart + 500ms);
	pacer.NoteInput(kStart);

	CHECK(pacer.SleepFor(kStart + Core::FramePacer::kInputWindow + 100ms, kPointerReleased, kNoDeadline) ==
		  Clock::duration::zero());
}

TEST_CASE("FramePacer draws a few frames for a request, then sleeps")
{
	Core::FramePacer pacer;
	pacer.NoteRequest();

	for (int frame = 0; frame < Core::FramePacer::kRequestFrames; ++frame)
	{
		CHECK(pacer.SleepFor(kStart, kPointerReleased, kNoDeadline) == Clock::duration::zero());
		pacer.FrameDrawn();
	}

	CHECK(pacer.SleepFor(kStart, kPointerReleased, kNoDeadline) == Core::FramePacer::kIdleTick);
}

TEST_CASE("FramePacer requests in a row do not add up")
{
	Core::FramePacer pacer;
	pacer.NoteRequest();
	pacer.NoteRequest();
	pacer.NoteRequest();

	for (int frame = 0; frame < Core::FramePacer::kRequestFrames; ++frame)
	{
		pacer.FrameDrawn();
	}

	CHECK(pacer.SleepFor(kStart, kPointerReleased, kNoDeadline) == Core::FramePacer::kIdleTick);
}

TEST_CASE("FramePacer request frames still honour a deadline once spent")
{
	Core::FramePacer pacer;
	pacer.NoteRequest();

	for (int frame = 0; frame < Core::FramePacer::kRequestFrames; ++frame)
	{
		pacer.FrameDrawn();
	}

	CHECK(pacer.SleepFor(kStart, kPointerReleased, kStart + 250ms) == 250ms);
}

TEST_CASE("Redraw hands a request to one take")
{
	DiscardAnyStandingRequest();

	Core::Redraw::Request();

	CHECK(Core::Redraw::TakeRequest());
	CHECK_FALSE(Core::Redraw::TakeRequest());
}

TEST_CASE("Redraw wakes once for requests made before the loop takes them so a request per result is cheap")
{
	static int s_WakeCount = 0;
	s_WakeCount = 0;
	DiscardAnyStandingRequest();

	Core::Redraw::SetWaker([] { ++s_WakeCount; });
	Core::Redraw::Request();
	Core::Redraw::Request();
	CHECK(s_WakeCount == 1);

	(void)Core::Redraw::TakeRequest();
	Core::Redraw::Request();
	CHECK(s_WakeCount == 2);

	Core::Redraw::SetWaker(nullptr);
	DiscardAnyStandingRequest();
}

TEST_CASE("Redraw without a waker still records the request")
{
	DiscardAnyStandingRequest();

	Core::Redraw::Request();

	CHECK(Core::Redraw::TakeRequest());
}

TEST_CASE("Redraw reports a standing request without taking it")
{
	DiscardAnyStandingRequest();
	CHECK_FALSE(Core::Redraw::IsRequested());

	Core::Redraw::Request();

	CHECK(Core::Redraw::IsRequested());
	CHECK(Core::Redraw::IsRequested());
	CHECK(Core::Redraw::TakeRequest());
	CHECK_FALSE(Core::Redraw::IsRequested());
}

TEST_CASE("Redraw keeps the earliest deadline and clears it on take")
{
	DiscardAnyPendingDeadline();

	const Clock::time_point before = Clock::now();
	Core::Redraw::RequestIn(10s);
	Core::Redraw::RequestIn(2s);
	Core::Redraw::RequestIn(5s);
	const Clock::time_point after = Clock::now();

	const std::optional<Clock::time_point> deadline = Core::Redraw::TakeDeadline();
	REQUIRE(deadline.has_value());
	CHECK(*deadline >= before + 2s);
	CHECK(*deadline <= after + 2s);

	CHECK_FALSE(Core::Redraw::TakeDeadline().has_value());
}
