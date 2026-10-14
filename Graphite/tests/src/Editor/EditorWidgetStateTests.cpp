#include <doctest/doctest.h>

#include "Editor/EditorWidgetState.h"

using Graphite::BracketMemo;
using Graphite::BracketPair;
using Graphite::CaretBlinkRestarts;
using Graphite::HistoryHold;
using Graphite::HistoryStepIsDue;
using Graphite::kHistoryFastestInterval;
using Graphite::kHistoryFirstInterval;
using Graphite::kHistoryHoldDelay;
using Graphite::kHistoryHoldLapse;
using Graphite::TakeHistoryStep;

namespace {
	constexpr bool kFreshPress = false;
	constexpr bool kRepeat = true;
}

TEST_CASE("A fresh undo press always takes a step and starts the hold over")
{
	const HistoryHold earlier{.BeganAt = 1.0, .LastStepAt = 9.99};

	CHECK(HistoryStepIsDue(earlier, kFreshPress, 10.0));

	const HistoryHold after = TakeHistoryStep(earlier, kFreshPress, 10.0);

	CHECK(after.BeganAt == 10.0);
	CHECK(after.LastStepAt == 10.0);
}

TEST_CASE("A held undo waits out the hold delay before repeating")
{
	const HistoryHold hold = TakeHistoryStep(HistoryHold{}, kFreshPress, 10.0);

	CHECK_FALSE(HistoryStepIsDue(hold, kRepeat, 10.0 + kHistoryHoldDelay - 0.01));
	CHECK(HistoryStepIsDue(hold, kRepeat, 10.0 + kHistoryHoldDelay + kHistoryFirstInterval));
}

TEST_CASE("A held undo repeats at the first interval right after the delay")
{
	const double start = 10.0;
	HistoryHold hold = TakeHistoryStep(HistoryHold{}, kFreshPress, start);

	const double first = start + kHistoryHoldDelay + kHistoryFirstInterval;
	REQUIRE(HistoryStepIsDue(hold, kRepeat, first));
	hold = TakeHistoryStep(hold, kRepeat, first);

	CHECK(hold.BeganAt == start);
	CHECK(hold.LastStepAt == first);

	CHECK_FALSE(HistoryStepIsDue(hold, kRepeat, first + kHistoryFirstInterval * 0.9));
	CHECK(HistoryStepIsDue(hold, kRepeat, first + kHistoryFirstInterval));
}

TEST_CASE("A long-held undo tightens to the fastest interval and no further")
{
	const double start = 10.0;
	HistoryHold hold = TakeHistoryStep(HistoryHold{}, kFreshPress, start);

	const double frame = 1.0 / 60.0;
	double late = start;

	for (double now = start + frame; now < start + 60.0; now += frame)
	{
		if (HistoryStepIsDue(hold, kRepeat, now))
		{
			hold = TakeHistoryStep(hold, kRepeat, now);
			late = now;
		}
	}

	REQUIRE(hold.BeganAt == start);
	REQUIRE(late > start + 59.0);

	CHECK_FALSE(HistoryStepIsDue(hold, kRepeat, late + kHistoryFastestInterval * 0.9));
	CHECK(HistoryStepIsDue(hold, kRepeat, late + kHistoryFastestInterval + 1e-9));
}

TEST_CASE("A repeat against a stale hold steps once, like a fresh press, and then waits the delay")
{
	const HistoryHold stale = TakeHistoryStep(HistoryHold{}, kFreshPress, 10.0);
	const double now = 10.0 + kHistoryHoldLapse + 5.0;

	REQUIRE(HistoryStepIsDue(stale, kRepeat, now));

	const HistoryHold hold = TakeHistoryStep(stale, kRepeat, now);

	CHECK(hold.BeganAt == now);
	CHECK(hold.LastStepAt == now);

	CHECK_FALSE(HistoryStepIsDue(hold, kRepeat, now + kHistoryFastestInterval + 0.01));
	CHECK_FALSE(HistoryStepIsDue(hold, kRepeat, now + kHistoryHoldDelay - 0.01));
	CHECK(HistoryStepIsDue(hold, kRepeat, now + kHistoryHoldDelay + kHistoryFirstInterval));
}

TEST_CASE("A first repeat that comes late is still part of the hold that began at the press")
{
	const HistoryHold hold = TakeHistoryStep(HistoryHold{}, kFreshPress, 10.0);
	const double now = 11.0;

	REQUIRE(HistoryStepIsDue(hold, kRepeat, now));
	CHECK(TakeHistoryStep(hold, kRepeat, now).BeganAt == 10.0);
}

TEST_CASE("The caret blink restarts only when the document was not focused on the frame before")
{
	CHECK(CaretBlinkRestarts(Graphite::kNeverFocusedFrame, 1));
	CHECK(CaretBlinkRestarts(Graphite::kNeverFocusedFrame, 500));
	CHECK_FALSE(CaretBlinkRestarts(41, 42));
	CHECK(CaretBlinkRestarts(40, 42));
}

TEST_CASE("A bracket memo answers only for the caret and revision it was filled at")
{
	BracketMemo memo;

	CHECK_FALSE(memo.Holds(0, 0));

	memo.Fill(12, 3, BracketPair{.Open = 12, .Close = 40, .Matched = true});

	CHECK(memo.Holds(12, 3));
	CHECK_FALSE(memo.Holds(13, 3));
	CHECK_FALSE(memo.Holds(12, 4));
	REQUIRE(memo.Pair.has_value());
	CHECK(memo.Pair->Close == 40);

	memo.Fill(20, 3, std::nullopt);

	CHECK(memo.Holds(20, 3));
	CHECK_FALSE(memo.Pair.has_value());
}

TEST_CASE("A fresh widget state has no actions waiting")
{
	const Graphite::EditorWidgetState state;

	CHECK(state.PendingActions.empty());
}
