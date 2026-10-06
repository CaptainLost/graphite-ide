#include <doctest/doctest.h>

#include "Core/App/Redraw.h"
#include "Core/Platform/AsyncDialog.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <thread>

using Core::AsyncDialog;
using Core::Error;
using Core::ErrorCode;
using Core::Result;

namespace {
	using Picked = Result<std::filesystem::path>;

	constexpr std::chrono::seconds kGiveUpAfter{5};
	constexpr std::chrono::milliseconds kPollInterval{1};
	constexpr std::chrono::milliseconds kTimeToAskForARedrawAfterReturning{200};

	// Answers false once kGiveUpAfter has passed, so that a broken dialog fails the test instead
	// of hanging the suite.
	bool PollEachFrameUntil(AsyncDialog& dialog, const std::function<bool()>& condition)
	{
		const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kGiveUpAfter;

		while (!condition())
		{
			if (std::chrono::steady_clock::now() > deadline)
			{
				return false;
			}

			dialog.Poll();
			std::this_thread::sleep_for(kPollInterval);
		}

		return true;
	}

	struct HeldOpenPicker
	{
		// Shared, because a dialog abandoned by its owner still runs to the end.
		std::shared_ptr<std::promise<void>> Release = std::make_shared<std::promise<void>>();
		std::shared_ptr<std::atomic<bool>> Returned = std::make_shared<std::atomic<bool>>(false);

		AsyncDialog::Picker MakePicker() const
		{
			return [release = Release->get_future().share(), returned = Returned]() -> Picked {
				release.wait();
				returned->store(true);
				return std::filesystem::path("picked");
			};
		}
	};

	void WaitUntilPickerReturns(const HeldOpenPicker& heldPicker)
	{
		const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kGiveUpAfter;

		while (!heldPicker.Returned->load() && std::chrono::steady_clock::now() < deadline)
		{
			std::this_thread::sleep_for(kPollInterval);
		}
	}

	Picked PickSecond()
	{
		return std::filesystem::path("second");
	}

	bool StartSecondDialog(AsyncDialog& dialog, int& deliveredCount)
	{
		return dialog.Start(PickSecond, [&deliveredCount](Picked) { ++deliveredCount; });
	}
}

TEST_CASE("AsyncDialog runs the picker on a thread other than the one that started it")
{
	AsyncDialog dialog;
	std::thread::id pickerThread;
	bool delivered = false;

	REQUIRE(dialog.Start(
		[&]() -> Picked {
			pickerThread = std::this_thread::get_id();
			return std::filesystem::path("C:/work");
		},
		[&](Picked) { delivered = true; }));

	REQUIRE(PollEachFrameUntil(dialog, [&] { return delivered; }));
	CHECK(pickerThread != std::thread::id{});
	CHECK(pickerThread != std::this_thread::get_id());
}

TEST_CASE("AsyncDialog hands the result over only through Poll on the thread that polls and never from the picker's thread")
{
	AsyncDialog dialog;
	HeldOpenPicker heldPicker;
	std::optional<Picked> deliveredAnswer;
	std::thread::id answerHandlerThread;

	REQUIRE(dialog.Start(heldPicker.MakePicker(), [&](Picked picked) {
		answerHandlerThread = std::this_thread::get_id();
		deliveredAnswer = std::move(picked);
	}));

	heldPicker.Release->set_value();

	WaitUntilPickerReturns(heldPicker);
	REQUIRE(heldPicker.Returned->load());
	CHECK_FALSE(deliveredAnswer.has_value());

	REQUIRE(PollEachFrameUntil(dialog, [&] { return deliveredAnswer.has_value(); }));
	CHECK(answerHandlerThread == std::this_thread::get_id());
	REQUIRE(deliveredAnswer->HasValue());
	CHECK(deliveredAnswer->GetValue() == std::filesystem::path("picked"));
}

TEST_CASE("AsyncDialog passes a failed pick through unchanged")
{
	AsyncDialog dialog;
	std::optional<Picked> deliveredAnswer;

	REQUIRE(dialog.Start([]() -> Picked { return Error{.Code = ErrorCode::Unsupported, .Message = "no dialog here"}; },
						 [&](Picked picked) { deliveredAnswer = std::move(picked); }));

	REQUIRE(PollEachFrameUntil(dialog, [&] { return deliveredAnswer.has_value(); }));
	REQUIRE_FALSE(deliveredAnswer->HasValue());
	CHECK(deliveredAnswer->GetError().Code == ErrorCode::Unsupported);
	CHECK(deliveredAnswer->GetError().Message == "no dialog here");
}

TEST_CASE("AsyncDialog counts as open from Start until its result is handed over")
{
	AsyncDialog dialog;
	HeldOpenPicker heldPicker;
	bool delivered = false;

	CHECK_FALSE(dialog.IsOpen());

	REQUIRE(dialog.Start(heldPicker.MakePicker(), [&](Picked) { delivered = true; }));
	CHECK(dialog.IsOpen());

	dialog.Poll();
	CHECK(dialog.IsOpen());

	heldPicker.Release->set_value();
	REQUIRE(PollEachFrameUntil(dialog, [&] { return delivered; }));
	CHECK_FALSE(dialog.IsOpen());
}

TEST_CASE("AsyncDialog refuses a second dialog while one is open")
{
	AsyncDialog dialog;
	HeldOpenPicker heldPicker;
	bool firstDelivered = false;
	bool secondRan = false;

	REQUIRE(dialog.Start(heldPicker.MakePicker(), [&](Picked) { firstDelivered = true; }));

	CHECK_FALSE(dialog.Start(
		[&]() -> Picked {
			secondRan = true;
			return std::filesystem::path{};
		},
		[&](Picked) { secondRan = true; }));

	heldPicker.Release->set_value();
	REQUIRE(PollEachFrameUntil(dialog, [&] { return firstDelivered; }));
	CHECK_FALSE(secondRan);
}

TEST_CASE("AsyncDialog counts as closed when the callback runs so the callback can start the next dialog")
{
	AsyncDialog dialog;
	int deliveredCount = 0;
	const int bothDialogs = 2;

	REQUIRE(dialog.Start([]() -> Picked { return std::filesystem::path("first"); },
						 [&](Picked) {
							 ++deliveredCount;
							 CHECK_FALSE(dialog.IsOpen());
							 CHECK(StartSecondDialog(dialog, deliveredCount));
						 }));

	REQUIRE(PollEachFrameUntil(dialog, [&] { return deliveredCount == bothDialogs; }));
	CHECK_FALSE(dialog.IsOpen());
}

TEST_CASE("AsyncDialog destroyed while a dialog is open neither waits for it nor calls back")
{
	HeldOpenPicker heldPicker;
	bool delivered = false;

	{
		AsyncDialog dialog;
		REQUIRE(dialog.Start(heldPicker.MakePicker(), [&](Picked) { delivered = true; }));
	}

	heldPicker.Release->set_value();

	WaitUntilPickerReturns(heldPicker);

	CHECK(heldPicker.Returned->load());
	CHECK_FALSE(delivered);
}

TEST_CASE("AsyncDialog destroyed while a dialog is open asks for no redraw when it returns since Redraw may be destroyed past main by then")
{
	HeldOpenPicker heldPicker;

	{
		AsyncDialog dialog;
		REQUIRE(dialog.Start(heldPicker.MakePicker(), [](Picked) {}));
	}

	Core::Redraw::TakeRequest();
	heldPicker.Release->set_value();

	WaitUntilPickerReturns(heldPicker);

	std::this_thread::sleep_for(kTimeToAskForARedrawAfterReturning);

	CHECK(heldPicker.Returned->load());
	CHECK_FALSE(Core::Redraw::IsRequested());
}
