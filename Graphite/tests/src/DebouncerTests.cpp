#include <doctest/doctest.h>

#include "Git/Debouncer.h"

#include "Core/App/Redraw.h"

using Graphite::Debouncer;

namespace {

	void DrainRedraw()
	{
		Core::Redraw::TakeRequest();
		Core::Redraw::TakeDeadline();
	}
}

TEST_CASE("A debouncer that was never marked does not fire")
{
	Debouncer debouncer;

	CHECK_FALSE(debouncer.IsDirty());
	CHECK_FALSE(debouncer.Advance(10.0f));
}

TEST_CASE("A debouncer fires once the quiet time has passed, and only once")
{
	Debouncer debouncer;
	debouncer.Mark();

	CHECK(debouncer.IsDirty());
	CHECK_FALSE(debouncer.Advance(Debouncer::kQuietSeconds * 0.5f));
	CHECK(debouncer.Advance(Debouncer::kQuietSeconds * 0.5f));
	CHECK_FALSE(debouncer.IsDirty());
	CHECK_FALSE(debouncer.Advance(10.0f));
}

TEST_CASE("A mark inside the burst restarts the quiet time")
{
	Debouncer debouncer;
	debouncer.Mark();

	CHECK_FALSE(debouncer.Advance(Debouncer::kQuietSeconds * 0.75f));
	debouncer.Mark();
	CHECK_FALSE(debouncer.Advance(Debouncer::kQuietSeconds * 0.75f));
	CHECK(debouncer.Advance(Debouncer::kQuietSeconds * 0.5f));
}

TEST_CASE("A sustained stream of marks still fires at the cap")
{
	Debouncer debouncer;
	debouncer.Mark();

	bool fired = false;
	float elapsed = 0.0f;
	const float step = Debouncer::kQuietSeconds * 0.5f;

	while (!fired && elapsed < Debouncer::kMaxSeconds * 2.0f)
	{
		fired = debouncer.Advance(step);
		elapsed += step;

		if (!fired)
		{
			debouncer.Mark();
		}
	}

	CHECK(fired);
	CHECK(elapsed >= Debouncer::kMaxSeconds);
	CHECK(elapsed < Debouncer::kMaxSeconds + step);
}

TEST_CASE("Reset forgets a pending burst without firing it")
{
	Debouncer debouncer;
	debouncer.Mark();
	debouncer.Reset();

	CHECK_FALSE(debouncer.IsDirty());
	CHECK_FALSE(debouncer.Advance(10.0f));
}

TEST_CASE("A burst's cap starts at its first mark, not after the previous burst")
{
	Debouncer debouncer;
	debouncer.Mark();
	CHECK(debouncer.Advance(Debouncer::kQuietSeconds));

	debouncer.Mark();
	CHECK_FALSE(debouncer.Advance(Debouncer::kQuietSeconds * 0.5f));
}

TEST_CASE("Marking asks for the frame the debounce runs out on")
{
	DrainRedraw();

	Debouncer debouncer;
	debouncer.Mark();

	const auto deadline = Core::Redraw::TakeDeadline();
	REQUIRE(deadline.has_value());
	CHECK(*deadline <= Core::Redraw::Clock::now() + std::chrono::duration<double>(Debouncer::kQuietSeconds));
}

TEST_CASE("A clean debouncer asks for no frame")
{
	Debouncer debouncer;
	DrainRedraw();

	CHECK_FALSE(debouncer.Advance(0.016f));
	CHECK_FALSE(Core::Redraw::TakeDeadline().has_value());
}
