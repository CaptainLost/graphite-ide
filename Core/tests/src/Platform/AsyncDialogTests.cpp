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

	void IgnoreFailure(const Error&) {}

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
		std::shared_ptr<std::atomic<bool>> Running = std::make_shared<std::atomic<bool>>(false);

		AsyncDialog::Picker MakePicker() const
		{
			return [release = Release->get_future().share(), returned = Returned, running = Running]() -> Picked {
				running->store(true);
				release.wait();
				returned->store(true);
				return std::filesystem::path("picked");
			};
		}
	};

	void WaitUntilPickerRuns(const HeldOpenPicker& heldPicker)
	{
		const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kGiveUpAfter;

		while (!heldPicker.Running->load() && std::chrono::steady_clock::now() < deadline)
		{
			std::this_thread::sleep_for(kPollInterval);
		}
	}

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
		return dialog.Start(PickSecond, [&deliveredCount](const std::filesystem::path&) { ++deliveredCount; }, IgnoreFailure);
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
		[&](const std::filesystem::path&) { delivered = true; }, IgnoreFailure));

	REQUIRE(PollEachFrameUntil(dialog, [&] { return delivered; }));
	CHECK(pickerThread != std::thread::id{});
	CHECK(pickerThread != std::this_thread::get_id());
}

TEST_CASE("AsyncDialog hands the result over only through Poll on the thread that polls and never from the picker's thread")
{
	AsyncDialog dialog;
	HeldOpenPicker heldPicker;
	std::optional<std::filesystem::path> deliveredPath;
	std::thread::id answerHandlerThread;
	bool failureReported = false;

	REQUIRE(dialog.Start(
		heldPicker.MakePicker(),
		[&](const std::filesystem::path& picked) {
			answerHandlerThread = std::this_thread::get_id();
			deliveredPath = picked;
		},
		[&](const Error&) { failureReported = true; }));

	heldPicker.Release->set_value();

	WaitUntilPickerReturns(heldPicker);
	REQUIRE(heldPicker.Returned->load());
	CHECK_FALSE(deliveredPath.has_value());

	REQUIRE(PollEachFrameUntil(dialog, [&] { return deliveredPath.has_value(); }));
	CHECK(answerHandlerThread == std::this_thread::get_id());
	CHECK(*deliveredPath == std::filesystem::path("picked"));
	CHECK_FALSE(failureReported);
}

TEST_CASE("AsyncDialog hands a failed pick to the failure handler alone, unchanged")
{
	AsyncDialog dialog;
	std::optional<Error> deliveredError;
	bool pathDelivered = false;

	REQUIRE(dialog.Start([]() -> Picked { return Error{.Code = ErrorCode::Unsupported, .Message = "no dialog here"}; },
						 [&](const std::filesystem::path&) { pathDelivered = true; },
						 [&](const Error& error) { deliveredError = error; }));

	REQUIRE(PollEachFrameUntil(dialog, [&] { return deliveredError.has_value(); }));
	CHECK(deliveredError->Code == ErrorCode::Unsupported);
	CHECK(deliveredError->Message == "no dialog here");
	CHECK_FALSE(pathDelivered);
}

TEST_CASE("AsyncDialog calls neither handler when the dialog is cancelled, which the picker answers with an empty path")
{
	AsyncDialog dialog;
	bool pathDelivered = false;
	bool failureReported = false;

	REQUIRE(dialog.Start([]() -> Picked { return std::filesystem::path{}; },
						 [&](const std::filesystem::path&) { pathDelivered = true; },
						 [&](const Error&) { failureReported = true; }));

	REQUIRE(PollEachFrameUntil(dialog, [&] { return !dialog.IsOpen(); }));
	CHECK_FALSE(pathDelivered);
	CHECK_FALSE(failureReported);
}

TEST_CASE("AsyncDialog counts as open from Start until its result is handed over")
{
	AsyncDialog dialog;
	HeldOpenPicker heldPicker;
	bool delivered = false;

	CHECK_FALSE(dialog.IsOpen());

	REQUIRE(dialog.Start(heldPicker.MakePicker(), [&](const std::filesystem::path&) { delivered = true; }, IgnoreFailure));
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

	REQUIRE(dialog.Start(heldPicker.MakePicker(), [&](const std::filesystem::path&) { firstDelivered = true; }, IgnoreFailure));

	CHECK_FALSE(dialog.Start(
		[&]() -> Picked {
			secondRan = true;
			return std::filesystem::path("second");
		},
		[&](const std::filesystem::path&) { secondRan = true; }, [&](const Error&) { secondRan = true; }));

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
						 [&](const std::filesystem::path&) {
							 ++deliveredCount;
							 CHECK_FALSE(dialog.IsOpen());
							 CHECK(StartSecondDialog(dialog, deliveredCount));
						 },
						 IgnoreFailure));

	REQUIRE(PollEachFrameUntil(dialog, [&] { return deliveredCount == bothDialogs; }));
	CHECK_FALSE(dialog.IsOpen());
}

TEST_CASE("AsyncDialog brings nothing to the front when no dialog is open")
{
	AsyncDialog dialog;

	dialog.BringToFront();

	CHECK_FALSE(dialog.IsOpen());
}

TEST_CASE("AsyncDialog bringing an open dialog to the front neither waits for the picker nor answers for it")
{
	AsyncDialog dialog;
	HeldOpenPicker heldPicker;
	bool delivered = false;
	REQUIRE(dialog.Start(heldPicker.MakePicker(), [&](const std::filesystem::path&) { delivered = true; }, IgnoreFailure));
	WaitUntilPickerRuns(heldPicker);
	REQUIRE(heldPicker.Running->load());

	dialog.BringToFront();
	dialog.Poll();

	CHECK(dialog.IsOpen());
	CHECK_FALSE(delivered);

	heldPicker.Release->set_value();
	REQUIRE(PollEachFrameUntil(dialog, [&] { return delivered; }));
}

TEST_CASE("AsyncDialog destroyed while a dialog is open neither waits for it nor calls back")
{
	HeldOpenPicker heldPicker;
	bool delivered = false;

	{
		AsyncDialog dialog;
		REQUIRE(dialog.Start(heldPicker.MakePicker(), [&](const std::filesystem::path&) { delivered = true; },
							 [&](const Error&) { delivered = true; }));
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
		REQUIRE(dialog.Start(heldPicker.MakePicker(), [](const std::filesystem::path&) {}, IgnoreFailure));
	}

	Core::Redraw::TakeRequest();
	heldPicker.Release->set_value();

	WaitUntilPickerReturns(heldPicker);

	std::this_thread::sleep_for(kTimeToAskForARedrawAfterReturning);

	CHECK(heldPicker.Returned->load());
	CHECK_FALSE(Core::Redraw::IsRequested());
}
