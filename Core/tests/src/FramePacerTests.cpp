#include <doctest/doctest.h>

#include "Core/App/FramePacer.h"
#include "Core/App/Redraw.h"

#include <chrono>

using namespace std::chrono_literals;

namespace {

	using Clock = Core::FramePacer::Clock;

	const Clock::time_point kStart = Clock::time_point{} + 1000s;
}

TEST_CASE("FramePacer draws every refresh for a while after input")
{
	Core::FramePacer pacer;
	pacer.NoteInput(kStart);

	CHECK(pacer.SleepFor(kStart, false, std::nullopt) == Clock::duration::zero());
	CHECK(pacer.SleepFor(kStart + Core::FramePacer::InputWindow - 1ms, false, std::nullopt) ==
		  Clock::duration::zero());
}

TEST_CASE("FramePacer sleeps for the idle tick once the input window has passed")
{
	Core::FramePacer pacer;
	pacer.NoteInput(kStart);

	const Clock::time_point later = kStart + Core::FramePacer::InputWindow;

	CHECK(pacer.SleepFor(later, false, std::nullopt) == Core::FramePacer::IdleTick);
}

TEST_CASE("FramePacer with no input yet sleeps")
{
	const Core::FramePacer pacer;

	CHECK(pacer.SleepFor(kStart, false, std::nullopt) == Core::FramePacer::IdleTick);
}

TEST_CASE("FramePacer wakes for a deadline sooner than the idle tick")
{
	const Core::FramePacer pacer;

	CHECK(pacer.SleepFor(kStart, false, kStart + 300ms) == 300ms);
}

TEST_CASE("FramePacer does not sleep past the idle tick for a later deadline")
{
	const Core::FramePacer pacer;

	CHECK(pacer.SleepFor(kStart, false, kStart + 1h) == Core::FramePacer::IdleTick);
}

TEST_CASE("FramePacer draws at once for a deadline already past")
{
	const Core::FramePacer pacer;

	CHECK(pacer.SleepFor(kStart, false, kStart - 5s) == Clock::duration::zero());
}

TEST_CASE("FramePacer keeps drawing while the pointer is held")
{
	const Core::FramePacer pacer;

	CHECK(pacer.SleepFor(kStart, true, std::nullopt) == Clock::duration::zero());
}

TEST_CASE("FramePacer input never shortens a window already running")
{
	Core::FramePacer pacer;
	pacer.NoteInput(kStart + 500ms);
	pacer.NoteInput(kStart);

	CHECK(pacer.SleepFor(kStart + Core::FramePacer::InputWindow + 100ms, false, std::nullopt) ==
		  Clock::duration::zero());
}

TEST_CASE("FramePacer draws a few frames for a request, then sleeps")
{
	Core::FramePacer pacer;
	pacer.NoteRequest();

	for (int frame = 0; frame < Core::FramePacer::RequestFrames; ++frame)
	{
		CHECK(pacer.SleepFor(kStart, false, std::nullopt) == Clock::duration::zero());
		pacer.FrameDrawn();
	}

	CHECK(pacer.SleepFor(kStart, false, std::nullopt) == Core::FramePacer::IdleTick);
}

TEST_CASE("FramePacer requests in a row do not add up")
{
	Core::FramePacer pacer;
	pacer.NoteRequest();
	pacer.NoteRequest();
	pacer.NoteRequest();

	for (int frame = 0; frame < Core::FramePacer::RequestFrames; ++frame)
		pacer.FrameDrawn();

	CHECK(pacer.SleepFor(kStart, false, std::nullopt) == Core::FramePacer::IdleTick);
}

TEST_CASE("FramePacer request frames still honour a deadline once spent")
{
	Core::FramePacer pacer;
	pacer.NoteRequest();

	for (int frame = 0; frame < Core::FramePacer::RequestFrames; ++frame)
		pacer.FrameDrawn();

	CHECK(pacer.SleepFor(kStart, false, kStart + 250ms) == 250ms);
}

TEST_CASE("Redraw hands a request to one take")
{
	(void)Core::Redraw::TakeRequest();

	Core::Redraw::Request();

	CHECK(Core::Redraw::TakeRequest());
	CHECK_FALSE(Core::Redraw::TakeRequest());
}

TEST_CASE("Redraw wakes once for requests made before the loop takes them")
{
	static int s_Wakes = 0;
	s_Wakes = 0;
	(void)Core::Redraw::TakeRequest();

	Core::Redraw::SetWaker([] { ++s_Wakes; });
	Core::Redraw::Request();
	Core::Redraw::Request();
	CHECK(s_Wakes == 1);

	(void)Core::Redraw::TakeRequest();
	Core::Redraw::Request();
	CHECK(s_Wakes == 2);

	Core::Redraw::SetWaker(nullptr);
	(void)Core::Redraw::TakeRequest();
}

TEST_CASE("Redraw without a waker still records the request")
{
	(void)Core::Redraw::TakeRequest();

	Core::Redraw::Request();

	CHECK(Core::Redraw::TakeRequest());
}

TEST_CASE("Redraw reports a standing request without taking it")
{
	(void)Core::Redraw::TakeRequest();
	CHECK_FALSE(Core::Redraw::IsRequested());

	Core::Redraw::Request();

	CHECK(Core::Redraw::IsRequested());
	CHECK(Core::Redraw::IsRequested());
	CHECK(Core::Redraw::TakeRequest());
	CHECK_FALSE(Core::Redraw::IsRequested());
}

TEST_CASE("Redraw keeps the earliest deadline and clears it on take")
{
	(void)Core::Redraw::TakeDeadline();

	const auto before = Clock::now();
	Core::Redraw::RequestIn(10s);
	Core::Redraw::RequestIn(2s);
	Core::Redraw::RequestIn(5s);
	const auto after = Clock::now();

	const auto deadline = Core::Redraw::TakeDeadline();
	REQUIRE(deadline.has_value());
	CHECK(*deadline >= before + 2s);
	CHECK(*deadline <= after + 2s);

	CHECK_FALSE(Core::Redraw::TakeDeadline().has_value());
}
