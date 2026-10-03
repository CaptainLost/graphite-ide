#include <doctest/doctest.h>

#include "Core/App/Redraw.h"
#include "Core/Platform/AsyncDialog.h"

#include <atomic>
#include <chrono>
#include <filesystem>
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

	// Polls the way a frame loop does until `done` holds or a generous deadline passes, so a
	// broken dialog fails the test instead of hanging the suite.
	template<typename Predicate>
	bool PollUntil(AsyncDialog& dialog, Predicate done)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

		while (!done())
		{
			if (std::chrono::steady_clock::now() > deadline)
			{
				return false;
			}

			dialog.Poll();
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		return true;
	}

	// A picker that stays open until the test lets it return, like a dialog the user has not
	// answered yet. Shared, because a dialog abandoned by its owner still runs to the end.
	struct HeldPicker
	{
		std::shared_ptr<std::promise<void>> Release = std::make_shared<std::promise<void>>();
		std::shared_ptr<std::atomic<bool>> Returned = std::make_shared<std::atomic<bool>>(false);

		AsyncDialog::Picker Make() const
		{
			return [release = Release->get_future().share(), returned = Returned]() -> Picked {
				release.wait();
				returned->store(true);
				return std::filesystem::path("picked");
			};
		}
	};
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

	REQUIRE(PollUntil(dialog, [&] { return delivered; }));
	CHECK(pickerThread != std::thread::id{});
	CHECK(pickerThread != std::this_thread::get_id());
}

TEST_CASE("AsyncDialog hands the result over on the thread that polls, and only through Poll")
{
	AsyncDialog dialog;
	HeldPicker picker;
	std::optional<Picked> result;
	std::thread::id doneThread;

	REQUIRE(dialog.Start(picker.Make(), [&](Picked picked) {
		doneThread = std::this_thread::get_id();
		result = std::move(picked);
	}));

	picker.Release->set_value();

	// Returned is not delivered: the picker's thread never calls back on its own.
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (!picker.Returned->load() && std::chrono::steady_clock::now() < deadline)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	REQUIRE(picker.Returned->load());
	CHECK_FALSE(result.has_value());

	REQUIRE(PollUntil(dialog, [&] { return result.has_value(); }));
	CHECK(doneThread == std::this_thread::get_id());
	REQUIRE(result->HasValue());
	CHECK(result->GetValue() == std::filesystem::path("picked"));
}

TEST_CASE("AsyncDialog passes a failed pick through unchanged")
{
	AsyncDialog dialog;
	std::optional<Picked> result;

	REQUIRE(dialog.Start([]() -> Picked { return Error{ErrorCode::Unsupported, "no dialog here"}; },
						 [&](Picked picked) { result = std::move(picked); }));

	REQUIRE(PollUntil(dialog, [&] { return result.has_value(); }));
	REQUIRE_FALSE(result->HasValue());
	CHECK(result->GetError().Code == ErrorCode::Unsupported);
	CHECK(result->GetError().Message == "no dialog here");
}

TEST_CASE("AsyncDialog counts as open from Start until its result is handed over")
{
	AsyncDialog dialog;
	HeldPicker picker;
	bool delivered = false;

	CHECK_FALSE(dialog.IsOpen());

	REQUIRE(dialog.Start(picker.Make(), [&](Picked) { delivered = true; }));
	CHECK(dialog.IsOpen());

	dialog.Poll();
	CHECK(dialog.IsOpen());

	picker.Release->set_value();
	REQUIRE(PollUntil(dialog, [&] { return delivered; }));
	CHECK_FALSE(dialog.IsOpen());
}

TEST_CASE("AsyncDialog refuses a second dialog while one is open")
{
	AsyncDialog dialog;
	HeldPicker picker;
	bool firstDelivered = false;
	bool secondRan = false;

	REQUIRE(dialog.Start(picker.Make(), [&](Picked) { firstDelivered = true; }));

	CHECK_FALSE(dialog.Start(
		[&]() -> Picked {
			secondRan = true;
			return std::filesystem::path{};
		},
		[&](Picked) { secondRan = true; }));

	picker.Release->set_value();
	REQUIRE(PollUntil(dialog, [&] { return firstDelivered; }));
	CHECK_FALSE(secondRan);
}

TEST_CASE("AsyncDialog lets the callback start the next dialog")
{
	AsyncDialog dialog;
	int delivered = 0;

	REQUIRE(dialog.Start([]() -> Picked { return std::filesystem::path("first"); },
						 [&](Picked) {
							 ++delivered;
							 CHECK(dialog.Start([]() -> Picked { return std::filesystem::path("second"); },
												[&](Picked) { ++delivered; }));
						 }));

	REQUIRE(PollUntil(dialog, [&] { return delivered == 2; }));
	CHECK_FALSE(dialog.IsOpen());
}

TEST_CASE("AsyncDialog destroyed while a dialog is open neither waits for it nor calls back")
{
	HeldPicker picker;
	bool delivered = false;

	{
		AsyncDialog dialog;
		REQUIRE(dialog.Start(picker.Make(), [&](Picked) { delivered = true; }));
	}

	// Still open, and the owner is gone: letting it return now must not reach the callback.
	picker.Release->set_value();

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (!picker.Returned->load() && std::chrono::steady_clock::now() < deadline)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	CHECK(picker.Returned->load());
	CHECK_FALSE(delivered);
}

// A dialog the owner gave up on may return after main has, while static objects are being
// destroyed - Redraw's among them. So once its owner is gone, its thread asks for no frame.
TEST_CASE("AsyncDialog destroyed while a dialog is open asks for no redraw when it returns")
{
	HeldPicker picker;

	{
		AsyncDialog dialog;
		REQUIRE(dialog.Start(picker.Make(), [](Picked) {}));
	}

	Core::Redraw::TakeRequest();
	picker.Release->set_value();

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (!picker.Returned->load() && std::chrono::steady_clock::now() < deadline)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	// Returned is set before the thread would ask; give it time to, so a regression shows.
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	CHECK(picker.Returned->load());
	CHECK_FALSE(Core::Redraw::IsRequested());
}
