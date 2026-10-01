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
using Graphite::TakeHistoryStep;

TEST_CASE("A fresh undo press always takes a step and starts the hold over")
{
	const HistoryHold earlier{.BeganAt = 1.0, .LastStepAt = 9.99};

	CHECK(HistoryStepIsDue(earlier, false, 10.0));

	const HistoryHold after = TakeHistoryStep(earlier, false, 10.0);

	CHECK(after.BeganAt == 10.0);
	CHECK(after.LastStepAt == 10.0);
}

TEST_CASE("A held undo waits out the hold delay before repeating")
{
	const HistoryHold hold = TakeHistoryStep({}, false, 10.0);

	CHECK_FALSE(HistoryStepIsDue(hold, true, 10.0 + kHistoryHoldDelay - 0.01));
	CHECK(HistoryStepIsDue(hold, true, 10.0 + kHistoryHoldDelay + kHistoryFirstInterval));
}

TEST_CASE("A held undo repeats at the first interval right after the delay")
{
	const double start = 10.0;
	HistoryHold hold = TakeHistoryStep({}, false, start);

	const double first = start + kHistoryHoldDelay + kHistoryFirstInterval;
	REQUIRE(HistoryStepIsDue(hold, true, first));
	hold = TakeHistoryStep(hold, true, first);

	CHECK(hold.BeganAt == start);
	CHECK(hold.LastStepAt == first);

	// Still under the first interval, less the little the ramp has taken off it since.
	CHECK_FALSE(HistoryStepIsDue(hold, true, first + kHistoryFirstInterval * 0.9));
	CHECK(HistoryStepIsDue(hold, true, first + kHistoryFirstInterval));
}

TEST_CASE("A long-held undo tightens to the fastest interval and no further")
{
	const double start = 10.0;
	HistoryHold hold = TakeHistoryStep({}, false, start);

	// Long past the point the ramp bottoms out.
	const double late = start + 60.0;
	REQUIRE(HistoryStepIsDue(hold, true, late));
	hold = TakeHistoryStep(hold, true, late);

	CHECK_FALSE(HistoryStepIsDue(hold, true, late + kHistoryFastestInterval * 0.9));
	CHECK(HistoryStepIsDue(hold, true, late + kHistoryFastestInterval + 1e-9));
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

	// Nothing to highlight is an answer too, and kept like any other.
	memo.Fill(20, 3, std::nullopt);

	CHECK(memo.Holds(20, 3));
	CHECK_FALSE(memo.Pair.has_value());
}
