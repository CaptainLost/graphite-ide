#include <doctest/doctest.h>

#include "Editor/Hover/HoverTracker.h"

using namespace Graphite;

namespace {
	constexpr HoverToken kFirstToken{3, 10, 18};
	constexpr HoverToken kSecondToken{3, 20, 25};

	HoverInput InputAt(double now, std::optional<HoverToken> token = kFirstToken)
	{
		HoverInput input;
		input.Token = token;
		input.Now = now;
		return input;
	}
}

TEST_CASE("Nothing is asked before the delay, and the tracker asks to be woken at it")
{
	HoverTracker tracker;

	const HoverActions first = tracker.Update(InputAt(10.0));
	CHECK_FALSE(first.AskInfo);
	REQUIRE(first.WakeAt.has_value());
	CHECK(*first.WakeAt == doctest::Approx(10.5));

	CHECK_FALSE(tracker.Update(InputAt(10.4)).AskInfo);
	CHECK(tracker.Update(InputAt(10.5)).AskInfo);
}

TEST_CASE("Information is asked once, then shown when it arrives")
{
	HoverTracker tracker;
	tracker.Update(InputAt(0.0));
	CHECK(tracker.Update(InputAt(0.6)).AskInfo);
	CHECK_FALSE(tracker.Update(InputAt(0.7)).AskInfo);

	HoverInput arrived = InputAt(0.8);
	arrived.InfoArrived = true;
	const HoverActions shown = tracker.Update(arrived);

	CHECK(shown.ShowInfo);
	CHECK_FALSE(shown.ShowPreview);
}

TEST_CASE("Moving to another token restarts the delay and cancels what was asked")
{
	HoverTracker tracker;
	tracker.Update(InputAt(0.0));
	tracker.Update(InputAt(0.6));

	const HoverActions moved = tracker.Update(InputAt(0.7, kSecondToken));
	CHECK(moved.Cancel);
	CHECK_FALSE(moved.AskInfo);
	CHECK(tracker.Update(InputAt(1.2, kSecondToken)).AskInfo);
}

TEST_CASE("Moving within the same token does not restart the delay")
{
	HoverTracker tracker;
	tracker.Update(InputAt(0.0));
	CHECK(tracker.Update(InputAt(0.5, kFirstToken)).AskInfo);
}

TEST_CASE("Leaving the token cancels and closes")
{
	HoverTracker tracker;
	tracker.Update(InputAt(0.0));
	tracker.Update(InputAt(0.6));

	const HoverActions left = tracker.Update(InputAt(0.7, std::nullopt));
	CHECK(left.Cancel);
	CHECK_FALSE(left.Visible());
}

TEST_CASE("An answer for an older version is not shown")
{
	HoverTracker tracker;
	tracker.Update(InputAt(0.0));
	tracker.Update(InputAt(0.6));

	HoverInput edited = InputAt(0.7);
	edited.Revision = 2;
	edited.InfoArrived = true;
	const HoverActions after = tracker.Update(edited);

	CHECK(after.Cancel);
	CHECK_FALSE(after.ShowInfo);
}

TEST_CASE("With Ctrl, the preview replaces the information, and is asked at once after the delay")
{
	HoverTracker tracker;
	tracker.Update(InputAt(0.0));
	tracker.Update(InputAt(0.6));

	HoverInput ctrl = InputAt(0.7);
	ctrl.Ctrl = true;
	const HoverActions swapped = tracker.Update(ctrl);

	CHECK(swapped.AskPreview);

	ctrl.Now = 0.8;
	ctrl.PreviewArrived = true;
	ctrl.InfoArrived = true;
	const HoverActions shown = tracker.Update(ctrl);

	CHECK(shown.ShowPreview);
	CHECK_FALSE(shown.ShowInfo);
}

TEST_CASE("Entering the tooltip latches it: releasing Ctrl and leaving the token keep it")
{
	HoverTracker tracker;
	HoverInput input = InputAt(0.0);
	input.Ctrl = true;
	tracker.Update(input);
	input.Now = 0.6;
	tracker.Update(input);

	HoverInput inside = InputAt(0.7, std::nullopt);
	inside.MouseInTooltip = true;
	inside.Ctrl = false;
	inside.PreviewArrived = true;
	const HoverActions held = tracker.Update(inside);

	CHECK(held.ShowPreview);
	CHECK_FALSE(held.Cancel);

	const HoverActions gone = tracker.Update(InputAt(0.8, std::nullopt));
	CHECK_FALSE(gone.Visible());
}

TEST_CASE("Input of any kind closes the tooltip")
{
	HoverTracker tracker;
	tracker.Update(InputAt(0.0));
	tracker.Update(InputAt(0.6));

	HoverInput typed = InputAt(0.7);
	typed.InputHappened = true;
	typed.InfoArrived = true;

	const HoverActions closed = tracker.Update(typed);
	CHECK(closed.Cancel);
	CHECK_FALSE(closed.Visible());
}

TEST_CASE("Each settings combination asks exactly what the settings table says")
{
	struct Row
	{
		bool Info;
		DefinitionPreviewMode Preview;
		bool Ctrl;
		bool AskInfo;
		bool AskPreview;
	};

	const Row rows[] = {
		{true, DefinitionPreviewMode::Off, false, true, false},
		{true, DefinitionPreviewMode::Off, true, true, false},
		{true, DefinitionPreviewMode::WithCtrl, false, true, false},
		{true, DefinitionPreviewMode::WithCtrl, true, false, true},
		{true, DefinitionPreviewMode::Always, false, true, true},
		{true, DefinitionPreviewMode::Always, true, true, true},
		{false, DefinitionPreviewMode::Off, false, false, false},
		{false, DefinitionPreviewMode::Off, true, false, false},
		{false, DefinitionPreviewMode::WithCtrl, false, false, false},
		{false, DefinitionPreviewMode::WithCtrl, true, false, true},
		{false, DefinitionPreviewMode::Always, false, false, true},
		{false, DefinitionPreviewMode::Always, true, false, true},
	};

	for (const Row& row : rows)
	{
		CAPTURE(row.Info);
		CAPTURE(static_cast<int>(row.Preview));
		CAPTURE(row.Ctrl);

		HoverTracker tracker;
		HoverInput input = InputAt(0.0);
		input.Settings.ShowInfo = row.Info;
		input.Settings.Preview = row.Preview;
		input.Ctrl = row.Ctrl;
		tracker.Update(input);

		input.Now = 1.0;
		const HoverActions asked = tracker.Update(input);

		CHECK(asked.AskInfo == row.AskInfo);
		CHECK(asked.AskPreview == row.AskPreview);
	}
}

TEST_CASE("A diagnostic alone shows immediately, with nothing asked")
{
	HoverTracker tracker;
	HoverInput input = InputAt(0.0);
	input.Settings.ShowInfo = false;
	input.Settings.Preview = DefinitionPreviewMode::Off;
	input.DiagnosticUnderMouse = true;

	const HoverActions shown = tracker.Update(input);

	CHECK(shown.ShowDiagnostic);
	CHECK_FALSE(shown.AskInfo);
	CHECK_FALSE(shown.WakeAt.has_value());
}

TEST_CASE("A diagnostic over a token shows at once and heads the information later")
{
	HoverTracker tracker;
	HoverInput input = InputAt(0.0);
	input.DiagnosticUnderMouse = true;

	CHECK(tracker.Update(input).ShowDiagnostic);

	input.Now = 0.6;
	tracker.Update(input);
	input.Now = 0.7;
	input.InfoArrived = true;
	const HoverActions both = tracker.Update(input);

	CHECK(both.ShowDiagnostic);
	CHECK(both.ShowInfo);
}

TEST_CASE("An answer already held for the token is not asked again")
{
	HoverTracker tracker;
	HoverInput input = InputAt(0.0);
	input.InfoArrived = true;
	tracker.Update(input);

	input.Now = 0.6;
	const HoverActions after = tracker.Update(input);

	CHECK_FALSE(after.AskInfo);
	CHECK(after.ShowInfo);
}

namespace {
	void RestUntilInfoIsShown(HoverTracker& tracker)
	{
		tracker.Update(InputAt(0.0));
		tracker.Update(InputAt(0.6));
		HoverInput arrived = InputAt(0.7);
		arrived.InfoArrived = true;
		REQUIRE(tracker.Update(arrived).ShowInfo);
	}

	void RestWithCtrlUntilPreviewIsShown(HoverTracker& tracker)
	{
		HoverInput input = InputAt(0.0);
		input.Ctrl = true;
		tracker.Update(input);
		input.Now = 0.6;
		tracker.Update(input);
		input.Now = 0.7;
		input.PreviewArrived = true;
		REQUIRE(tracker.Update(input).ShowPreview);
	}
}

TEST_CASE("Typing hides a diagnostic under a still mouse until the mouse moves")
{
	HoverTracker tracker;
	HoverInput input = InputAt(0.0, std::nullopt);
	input.DiagnosticUnderMouse = true;
	REQUIRE(tracker.Update(input).ShowDiagnostic);

	input.Now = 0.1;
	input.InputHappened = true;
	CHECK_FALSE(tracker.Update(input).Visible());

	input.InputHappened = false;
	input.Now = 0.2;
	CHECK_FALSE(tracker.Update(input).Visible());
	input.Now = 3.0;
	CHECK_FALSE(tracker.Update(input).Visible());

	input.Now = 3.1;
	input.MouseMoved = true;
	CHECK(tracker.Update(input).ShowDiagnostic);
}

TEST_CASE("Typing hides the information until the mouse moves, then the delay starts again")
{
	HoverTracker tracker;
	RestUntilInfoIsShown(tracker);

	HoverInput typed = InputAt(0.8);
	typed.InfoArrived = true;
	typed.InputHappened = true;
	CHECK_FALSE(tracker.Update(typed).Visible());

	HoverInput still = InputAt(2.0);
	still.InfoArrived = true;
	CHECK_FALSE(tracker.Update(still).Visible());

	HoverInput moved = InputAt(2.1);
	moved.InfoArrived = true;
	moved.MouseMoved = true;
	CHECK_FALSE(tracker.Update(moved).ShowInfo);

	HoverInput rested = InputAt(2.6);
	rested.InfoArrived = true;
	const HoverActions shown = tracker.Update(rested);
	CHECK(shown.ShowInfo);
	CHECK_FALSE(shown.AskInfo);
}

TEST_CASE("A dismissed tracker asks nothing, wakes for nothing and cancels only once")
{
	HoverTracker tracker;
	tracker.Update(InputAt(0.0));
	tracker.Update(InputAt(0.6));

	HoverInput typed = InputAt(0.7);
	typed.InputHappened = true;
	CHECK(tracker.Update(typed).Cancel);

	typed.Now = 0.8;
	CHECK_FALSE(tracker.Update(typed).Cancel);

	for (const double now : {1.0, 2.0, 5.0})
	{
		const HoverActions idle = tracker.Update(InputAt(now, kSecondToken));
		CHECK_FALSE(idle.AskInfo);
		CHECK_FALSE(idle.AskPreview);
		CHECK_FALSE(idle.Cancel);
		CHECK_FALSE(idle.WakeAt.has_value());
		CHECK_FALSE(idle.Visible());
	}
}

TEST_CASE("Leaving the token and reaching the tooltip within the grace keeps the preview, latched")
{
	HoverTracker tracker;
	RestWithCtrlUntilPreviewIsShown(tracker);

	HoverInput between = InputAt(0.75, std::nullopt);
	between.PreviewArrived = true;
	const HoverActions transit = tracker.Update(between);
	CHECK(transit.ShowPreview);
	CHECK_FALSE(transit.Cancel);
	REQUIRE(transit.WakeAt.has_value());
	CHECK(*transit.WakeAt == doctest::Approx(1.0));

	HoverInput inside = InputAt(0.95, std::nullopt);
	inside.MouseInTooltip = true;
	inside.PreviewArrived = true;
	const HoverActions reached = tracker.Update(inside);
	CHECK(reached.ShowPreview);
	CHECK_FALSE(reached.Cancel);
	CHECK(tracker.Current() == kFirstToken);

	inside.Now = 3.0;
	const HoverActions later = tracker.Update(inside);
	CHECK(later.ShowPreview);
	CHECK_FALSE(later.ShowInfo);
}

TEST_CASE("Leaving both the token and the tooltip for longer than the grace closes")
{
	HoverTracker tracker;
	tracker.Update(InputAt(0.0));
	tracker.Update(InputAt(0.6));
	HoverInput shownInput = InputAt(0.7);
	shownInput.InfoArrived = true;
	shownInput.DiagnosticUnderMouse = true;
	REQUIRE(tracker.Update(shownInput).ShowInfo);

	CHECK(tracker.Update(InputAt(0.75, std::nullopt)).ShowDiagnostic);

	const HoverActions closed = tracker.Update(InputAt(1.1, std::nullopt));
	CHECK(closed.Cancel);
	CHECK_FALSE(closed.Visible());
	CHECK_FALSE(tracker.Current().has_value());
}

TEST_CASE("The diagnostic stays while the mouse is inside the tooltip")
{
	HoverTracker tracker;
	HoverInput input = InputAt(0.0);
	input.DiagnosticUnderMouse = true;
	tracker.Update(input);
	input.Now = 0.6;
	tracker.Update(input);
	input.Now = 0.7;
	input.InfoArrived = true;
	REQUIRE(tracker.Update(input).ShowDiagnostic);

	HoverInput inside = InputAt(0.8, std::nullopt);
	inside.MouseInTooltip = true;
	inside.InfoArrived = true;
	const HoverActions held = tracker.Update(inside);
	CHECK(held.ShowDiagnostic);
	CHECK(held.ShowInfo);
}

TEST_CASE("Ctrl pressed before the delay asks for the preview exactly once, at the delay")
{
	HoverTracker tracker;
	tracker.Update(InputAt(0.0));

	HoverInput ctrl = InputAt(0.2);
	ctrl.Ctrl = true;
	const HoverActions early = tracker.Update(ctrl);
	CHECK_FALSE(early.AskPreview);
	CHECK_FALSE(early.AskInfo);

	ctrl.Now = 0.5;
	const HoverActions due = tracker.Update(ctrl);
	CHECK(due.AskPreview);
	CHECK_FALSE(due.AskInfo);

	ctrl.Now = 0.6;
	CHECK_FALSE(tracker.Update(ctrl).AskPreview);
}

TEST_CASE("After the delay, an answer still pending wakes the tracker only to show that it is loading")
{
	HoverTracker tracker;
	tracker.Update(InputAt(0.0));
	REQUIRE(tracker.Update(InputAt(0.6)).AskInfo);

	const HoverActions pending = tracker.Update(InputAt(0.7));

	CHECK_FALSE(pending.InfoLoading);
	REQUIRE(pending.WakeAt.has_value());
	CHECK(*pending.WakeAt == doctest::Approx(0.6 + HoverTracker::kLoadingAfterSeconds));

	const HoverActions loading = tracker.Update(InputAt(0.6 + HoverTracker::kLoadingAfterSeconds));

	CHECK(loading.InfoLoading);
	CHECK(loading.Visible());
	CHECK_FALSE(loading.WakeAt.has_value());
}

TEST_CASE("The loader gives way to the answer when it arrives")
{
	HoverTracker tracker;
	tracker.Update(InputAt(0.0));
	REQUIRE(tracker.Update(InputAt(0.6)).AskInfo);
	REQUIRE(tracker.Update(InputAt(1.0)).InfoLoading);

	HoverInput arrived = InputAt(1.1);
	arrived.InfoArrived = true;
	const HoverActions shown = tracker.Update(arrived);

	CHECK(shown.ShowInfo);
	CHECK_FALSE(shown.InfoLoading);
}

TEST_CASE("A preview still pending shows that it is loading")
{
	HoverTracker tracker;

	HoverInput ctrl = InputAt(0.0);
	ctrl.Ctrl = true;
	tracker.Update(ctrl);
	ctrl.Now = 0.6;
	REQUIRE(tracker.Update(ctrl).AskPreview);

	ctrl.Now = 1.0;
	const HoverActions loading = tracker.Update(ctrl);

	CHECK(loading.PreviewLoading);
	CHECK_FALSE(loading.InfoLoading);
}

TEST_CASE("No loader shows while a failed answer waits for its retry, and the retry failing gives up")
{
	HoverTracker tracker;
	tracker.Update(InputAt(0.0));
	REQUIRE(tracker.Update(InputAt(0.6)).AskInfo);

	HoverInput failed = InputAt(0.7);
	failed.InfoFailed = true;
	failed.InfoFailureSaysWhy = true;
	const HoverActions waiting = tracker.Update(failed);

	CHECK_FALSE(waiting.InfoLoading);
	CHECK_FALSE(waiting.InfoGaveUp);

	failed.Now = 0.7 + HoverTracker::kHoverRetrySeconds;
	const HoverActions retried = tracker.Update(failed);

	REQUIRE(retried.AskInfo);
	CHECK_FALSE(retried.InfoGaveUp);

	const HoverActions retryPending = tracker.Update(InputAt(1.3));

	CHECK(retryPending.InfoLoading);

	failed.Now = 1.4;
	const HoverActions gaveUp = tracker.Update(failed);

	CHECK(gaveUp.InfoGaveUp);
	CHECK_FALSE(gaveUp.InfoLoading);
	CHECK(gaveUp.Visible());
}

TEST_CASE("A hover that gave up with nothing to say why shows nothing")
{
	HoverTracker tracker;
	tracker.Update(InputAt(0.0));
	REQUIRE(tracker.Update(InputAt(0.6)).AskInfo);

	HoverInput failed = InputAt(0.7);
	failed.InfoFailed = true;
	tracker.Update(failed);

	failed.Now = 0.7 + HoverTracker::kHoverRetrySeconds;
	REQUIRE(tracker.Update(failed).AskInfo);

	failed.Now = 1.4;
	const HoverActions gaveUp = tracker.Update(failed);

	CHECK_FALSE(gaveUp.InfoGaveUp);
	CHECK_FALSE(gaveUp.Visible());
}

TEST_CASE("Only a preview asked for by holding Ctrl reads as held by Ctrl")
{
	HoverTracker tracker;

	HoverInput ctrl = InputAt(0.0);
	ctrl.Ctrl = true;
	ctrl.Settings.Preview = DefinitionPreviewMode::WithCtrl;
	tracker.Update(ctrl);
	ctrl.Now = 0.6;

	CHECK(tracker.Update(ctrl).PreviewHeldByCtrl);

	HoverTracker always;

	HoverInput plain = InputAt(0.0);
	plain.Settings.Preview = DefinitionPreviewMode::Always;
	always.Update(plain);
	plain.Now = 0.6;

	const HoverActions asked = always.Update(plain);

	CHECK(asked.AskPreview);
	CHECK_FALSE(asked.PreviewHeldByCtrl);
}

TEST_CASE("A failed answer is asked once more after a pause, and only once per rest")
{
	HoverTracker tracker;
	tracker.Update(InputAt(0.0));
	REQUIRE(tracker.Update(InputAt(0.6)).AskInfo);

	HoverInput failed = InputAt(0.7);
	failed.InfoFailed = true;
	const HoverActions seen = tracker.Update(failed);
	CHECK_FALSE(seen.AskInfo);
	REQUIRE(seen.WakeAt.has_value());
	CHECK(*seen.WakeAt == doctest::Approx(0.7 + HoverTracker::kHoverRetrySeconds));

	failed.Now = 0.7 + HoverTracker::kHoverRetrySeconds;
	CHECK(tracker.Update(failed).AskInfo);

	failed.Now = 3.0;
	const HoverActions again = tracker.Update(failed);
	CHECK_FALSE(again.AskInfo);
	CHECK_FALSE(again.WakeAt.has_value());
	failed.Now = 10.0;
	CHECK_FALSE(tracker.Update(failed).AskInfo);
}

TEST_CASE("A failed preview is asked once more after a pause")
{
	HoverTracker tracker;

	HoverInput ctrl = InputAt(0.0);
	ctrl.Ctrl = true;
	tracker.Update(ctrl);
	ctrl.Now = 0.6;
	REQUIRE(tracker.Update(ctrl).AskPreview);

	ctrl.PreviewFailed = true;
	ctrl.Now = 0.7;
	CHECK_FALSE(tracker.Update(ctrl).AskPreview);
	ctrl.Now = 0.7 + HoverTracker::kHoverRetrySeconds;
	CHECK(tracker.Update(ctrl).AskPreview);
	ctrl.Now = 5.0;
	CHECK_FALSE(tracker.Update(ctrl).AskPreview);
}

TEST_CASE("A new rest earns a new retry")
{
	HoverTracker tracker;
	tracker.Update(InputAt(0.0));
	REQUIRE(tracker.Update(InputAt(0.6)).AskInfo);

	HoverInput failed = InputAt(0.7);
	failed.InfoFailed = true;
	tracker.Update(failed);
	failed.Now = 1.2;
	REQUIRE(tracker.Update(failed).AskInfo);

	tracker.Update(InputAt(2.0, kSecondToken));
	tracker.Update(InputAt(2.1));
	REQUIRE(tracker.Update(InputAt(2.6)).AskInfo);

	failed.Now = 2.7;
	tracker.Update(failed);
	failed.Now = 3.2;
	CHECK(tracker.Update(failed).AskInfo);
}

TEST_CASE("An answer that has arrived is not asked again, whatever failed before it")
{
	HoverTracker tracker;
	tracker.Update(InputAt(0.0));
	REQUIRE(tracker.Update(InputAt(0.6)).AskInfo);

	HoverInput both = InputAt(0.7);
	both.InfoFailed = true;
	both.InfoArrived = true;
	tracker.Update(both);
	both.Now = 1.5;
	CHECK_FALSE(tracker.Update(both).AskInfo);
}

TEST_CASE("A failure left over from an earlier rest neither delays nor hurries the retry")
{
	HoverTracker tracker;
	tracker.Update(InputAt(0.0));

	HoverInput stale = InputAt(0.6);
	stale.InfoFailed = true;
	const HoverActions asked = tracker.Update(stale);
	REQUIRE(asked.AskInfo);
	CHECK_FALSE(asked.WakeAt.has_value());

	CHECK_FALSE(tracker.Update(InputAt(1.0)).AskInfo);

	HoverInput failed = InputAt(2.0);
	failed.InfoFailed = true;
	CHECK_FALSE(tracker.Update(failed).AskInfo);
	failed.Now = 2.0 + HoverTracker::kHoverRetrySeconds - 0.01;
	CHECK_FALSE(tracker.Update(failed).AskInfo);
	failed.Now = 2.0 + HoverTracker::kHoverRetrySeconds;
	CHECK(tracker.Update(failed).AskInfo);
}
