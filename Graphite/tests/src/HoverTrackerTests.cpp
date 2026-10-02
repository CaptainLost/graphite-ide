#include <doctest/doctest.h>

#include "Editor/HoverTracker.h"

using namespace Graphite;

namespace {
	constexpr HoverToken kA{3, 10, 18};
	constexpr HoverToken kB{3, 20, 25};

	HoverInput At(double now, std::optional<HoverToken> token = kA)
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

	const HoverActions first = tracker.Update(At(10.0));
	CHECK_FALSE(first.AskInfo);
	REQUIRE(first.WakeAt.has_value());
	CHECK(*first.WakeAt == doctest::Approx(10.5));

	CHECK_FALSE(tracker.Update(At(10.4)).AskInfo);
	CHECK(tracker.Update(At(10.5)).AskInfo);
}

TEST_CASE("Information is asked once, then shown when it arrives")
{
	HoverTracker tracker;
	tracker.Update(At(0.0));
	CHECK(tracker.Update(At(0.6)).AskInfo);
	CHECK_FALSE(tracker.Update(At(0.7)).AskInfo);

	HoverInput arrived = At(0.8);
	arrived.InfoArrived = true;
	const HoverActions shown = tracker.Update(arrived);

	CHECK(shown.ShowInfo);
	CHECK_FALSE(shown.ShowPreview);
}

TEST_CASE("Moving to another token restarts the delay and cancels what was asked")
{
	HoverTracker tracker;
	tracker.Update(At(0.0));
	tracker.Update(At(0.6));

	const HoverActions moved = tracker.Update(At(0.7, kB));
	CHECK(moved.Cancel);
	CHECK_FALSE(moved.AskInfo);
	CHECK(tracker.Update(At(1.2, kB)).AskInfo);
}

TEST_CASE("Moving within the same token does not restart the delay")
{
	HoverTracker tracker;
	tracker.Update(At(0.0));
	CHECK(tracker.Update(At(0.5, kA)).AskInfo);
}

TEST_CASE("Leaving the token cancels and closes")
{
	HoverTracker tracker;
	tracker.Update(At(0.0));
	tracker.Update(At(0.6));

	const HoverActions left = tracker.Update(At(0.7, std::nullopt));
	CHECK(left.Cancel);
	CHECK_FALSE(left.Visible());
}

TEST_CASE("An answer for an older version is not shown")
{
	HoverTracker tracker;
	tracker.Update(At(0.0));
	tracker.Update(At(0.6));

	HoverInput edited = At(0.7);
	edited.Revision = 2;
	edited.InfoArrived = true; // the widget looked it up for revision 2; it was asked for revision 0
	const HoverActions after = tracker.Update(edited);

	CHECK(after.Cancel);
	CHECK_FALSE(after.ShowInfo);
}

TEST_CASE("With Ctrl, the preview replaces the information, and is asked at once after the delay")
{
	HoverTracker tracker;
	tracker.Update(At(0.0));
	tracker.Update(At(0.6));

	HoverInput ctrl = At(0.7);
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
	HoverInput input = At(0.0);
	input.Ctrl = true;
	tracker.Update(input);
	input.Now = 0.6;
	tracker.Update(input);

	HoverInput inside = At(0.7, std::nullopt);
	inside.MouseInTooltip = true;
	inside.Ctrl = false;
	inside.PreviewArrived = true;
	const HoverActions held = tracker.Update(inside);

	CHECK(held.ShowPreview);
	CHECK_FALSE(held.Cancel);

	const HoverActions gone = tracker.Update(At(0.8, std::nullopt));
	CHECK_FALSE(gone.Visible());
}

TEST_CASE("Input of any kind closes the tooltip")
{
	HoverTracker tracker;
	tracker.Update(At(0.0));
	tracker.Update(At(0.6));

	HoverInput typed = At(0.7);
	typed.InputHappened = true;
	typed.InfoArrived = true;

	const HoverActions closed = tracker.Update(typed);
	CHECK(closed.Cancel);
	CHECK_FALSE(closed.Visible());
}

TEST_CASE("Each settings combination asks exactly what the spec's table says")
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
		HoverInput input = At(0.0);
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
	HoverInput input = At(0.0);
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
	HoverInput input = At(0.0);
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
	HoverInput input = At(0.0);
	input.InfoArrived = true;
	tracker.Update(input);

	input.Now = 0.6;
	const HoverActions after = tracker.Update(input);

	CHECK_FALSE(after.AskInfo);
	CHECK(after.ShowInfo);
}
