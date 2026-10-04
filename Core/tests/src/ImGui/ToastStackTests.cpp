#include <doctest/doctest.h>

#include "Core/ImGui/ToastStack.h"

#include <chrono>

namespace {
	using Clock = Core::ToastStack::Clock;
	using namespace std::chrono_literals;

	const Clock::time_point kStart{};
}

TEST_CASE("A warning leaves once its lifetime has run and is removed after the fade")
{
	Core::ToastStack stack;
	stack.Show(Core::ToastKind::Warning, "Title", "careful", kStart);

	stack.Update(kStart + 5900ms);
	REQUIRE(stack.Get().size() == 1);
	CHECK_FALSE(stack.Get()[0].LeavingSince.has_value());

	stack.Update(kStart + 6s);
	REQUIRE(stack.Get().size() == 1);
	REQUIRE(stack.Get()[0].LeavingSince.has_value());
	CHECK(*stack.Get()[0].LeavingSince == kStart + 6s);

	stack.Update(kStart + 6s + Core::ToastStack::kLeave);
	CHECK(stack.Get().empty());
}

TEST_CASE("An error never expires and leaves only when closed")
{
	Core::ToastStack stack;
	stack.Show(Core::ToastKind::Error, "Title", "failed", kStart);

	stack.Update(kStart + 1h);
	REQUIRE(stack.Get().size() == 1);
	CHECK_FALSE(stack.Get()[0].LeavingSince.has_value());

	stack.Close(stack.Get()[0].Id, kStart + 1h);
	stack.Update(kStart + 1h + Core::ToastStack::kLeave);
	CHECK(stack.Get().empty());
}

TEST_CASE("Hovering pauses the countdown and leaving the toast resumes it with the time that was left")
{
	Core::ToastStack stack;
	stack.Show(Core::ToastKind::Success, "Title", "done", kStart);
	const std::uint64_t id = stack.Get()[0].Id;

	stack.SetHovered(id, true, kStart + 2s);
	stack.Update(kStart + 30s);
	REQUIRE(stack.Get().size() == 1);
	CHECK_FALSE(stack.Get()[0].LeavingSince.has_value());

	// 4 s were left when the pointer arrived.
	stack.SetHovered(id, false, kStart + 30s);
	stack.Update(kStart + 33s);
	CHECK_FALSE(stack.Get()[0].LeavingSince.has_value());

	stack.Update(kStart + 34s);
	CHECK(stack.Get()[0].LeavingSince.has_value());
}

TEST_CASE("Hover reported every frame does not restart the pause")
{
	Core::ToastStack stack;
	stack.Show(Core::ToastKind::Success, "Title", "done", kStart);
	const std::uint64_t id = stack.Get()[0].Id;

	stack.SetHovered(id, false, kStart + 1s);
	stack.SetHovered(id, false, kStart + 2s);
	stack.Update(kStart + 6s);
	CHECK(stack.Get()[0].LeavingSince.has_value());
}

TEST_CASE("An identical message merges into the toast still shown and counts and restarts its countdown")
{
	Core::ToastStack stack;
	stack.Show(Core::ToastKind::Warning, "Title", "same", kStart);
	stack.Show(Core::ToastKind::Warning, "Title", "same", kStart + 5s);

	REQUIRE(stack.Get().size() == 1);
	CHECK(stack.Get()[0].Count == 2);

	stack.Update(kStart + 10s);
	CHECK_FALSE(stack.Get()[0].LeavingSince.has_value());

	stack.Update(kStart + 11s);
	CHECK(stack.Get()[0].LeavingSince.has_value());
}

TEST_CASE("A message differing in kind or text or arriving while its twin leaves is a toast of its own")
{
	Core::ToastStack stack;
	stack.Show(Core::ToastKind::Warning, "Title", "same", kStart);
	stack.Show(Core::ToastKind::Error, "Title", "same", kStart);
	stack.Show(Core::ToastKind::Warning, "Title", "other", kStart);
	CHECK(stack.Get().size() == 3);

	stack.Close(stack.Get()[0].Id, kStart + 1s);
	stack.Show(Core::ToastKind::Warning, "Title", "same", kStart + 1s);
	CHECK(stack.Get().size() == 4);
}

TEST_CASE("The same message under a different title is a toast of its own")
{
	Core::ToastStack stack;
	stack.Show(Core::ToastKind::Error, "Stage failed", "access denied", kStart);
	stack.Show(Core::ToastKind::Error, "Commit failed", "access denied", kStart);

	REQUIRE(stack.Get().size() == 2);
	CHECK(stack.Get()[0].Title == "Stage failed");
	CHECK(stack.Get()[1].Title == "Commit failed");
	CHECK(stack.Get()[0].Count == 1);
	CHECK(stack.Get()[1].Count == 1);
}

TEST_CASE("The same title and message merge into one toast")
{
	Core::ToastStack stack;
	stack.Show(Core::ToastKind::Error, "Stage failed", "access denied", kStart);
	stack.Show(Core::ToastKind::Error, "Stage failed", "access denied", kStart + 1s);

	REQUIRE(stack.Get().size() == 1);
	CHECK(stack.Get()[0].Title == "Stage failed");
	CHECK(stack.Get()[0].Message == "access denied");
	CHECK(stack.Get()[0].Count == 2);
}

TEST_CASE("Toasts keep the order they were shown in and every one has its own id")
{
	Core::ToastStack stack;
	stack.Show(Core::ToastKind::Error, "Title", "first", kStart);
	stack.Show(Core::ToastKind::Error, "Title", "second", kStart);

	REQUIRE(stack.Get().size() == 2);
	CHECK(stack.Get()[0].Message == "first");
	CHECK(stack.Get()[1].Message == "second");
	CHECK(stack.Get()[0].Id != stack.Get()[1].Id);
}

TEST_CASE("The next change is the end of an animation or else the earliest expiry or else nothing")
{
	Core::ToastStack stack;
	CHECK_FALSE(stack.NextChange(kStart).has_value());

	stack.Show(Core::ToastKind::Warning, "Title", "w", kStart);
	// Entering: the renderer wants every frame until the fade-in ends.
	CHECK(stack.NextChange(kStart) == kStart);

	stack.Update(kStart + 1s);
	CHECK(stack.NextChange(kStart + 1s) == kStart + 6s);

	stack.SetHovered(stack.Get()[0].Id, true, kStart + 1s);
	CHECK_FALSE(stack.NextChange(kStart + 1s).has_value());
}

TEST_CASE("An error waiting to be closed asks for no frame")
{
	Core::ToastStack stack;
	stack.Show(Core::ToastKind::Error, "Title", "e", kStart);
	stack.Update(kStart + 1s);
	CHECK_FALSE(stack.NextChange(kStart + 1s).has_value());
}

TEST_CASE("Hover reported on two frames takes the time it had counted off only once")
{
	Core::ToastStack stack;
	stack.Show(Core::ToastKind::Success, "Title", "done", kStart);
	const std::uint64_t id = stack.Get()[0].Id;

	// 4 s left when the pointer arrives; the second report must not take another 3 s off.
	stack.SetHovered(id, true, kStart + 2s);
	stack.SetHovered(id, true, kStart + 5s);
	stack.SetHovered(id, false, kStart + 10s);

	stack.Update(kStart + 13900ms);
	REQUIRE(stack.Get().size() == 1);
	CHECK_FALSE(stack.Get()[0].LeavingSince.has_value());

	stack.Update(kStart + 14s);
	REQUIRE(stack.Get()[0].LeavingSince.has_value());
	CHECK(*stack.Get()[0].LeavingSince == kStart + 14s);
}

TEST_CASE("A message merged into a hovered twin keeps it paused and gives it the full lifetime")
{
	Core::ToastStack stack;
	stack.Show(Core::ToastKind::Warning, "Title", "same", kStart);
	const std::uint64_t id = stack.Get()[0].Id;

	stack.SetHovered(id, true, kStart + 2s);
	stack.Show(Core::ToastKind::Warning, "Title", "same", kStart + 3s);

	REQUIRE(stack.Get().size() == 1);
	CHECK(stack.Get()[0].Count == 2);
	CHECK_FALSE(stack.Get()[0].CountingFrom.has_value());
	CHECK(stack.Get()[0].Left == Core::ToastStack::kLifetime);

	stack.Update(kStart + 30s);
	REQUIRE(stack.Get().size() == 1);
	CHECK_FALSE(stack.Get()[0].LeavingSince.has_value());

	stack.SetHovered(id, false, kStart + 30s);
	stack.Update(kStart + 35900ms);
	REQUIRE(stack.Get().size() == 1);
	CHECK_FALSE(stack.Get()[0].LeavingSince.has_value());

	stack.Update(kStart + 36s);
	REQUIRE(stack.Get().size() == 1);
	REQUIRE(stack.Get()[0].LeavingSince.has_value());
	CHECK(*stack.Get()[0].LeavingSince == kStart + 36s);
}

TEST_CASE("Clear drops every toast and leaves nothing to wait for")
{
	Core::ToastStack stack;
	stack.Show(Core::ToastKind::Error, "Title", "e", kStart);
	stack.Show(Core::ToastKind::Warning, "Title", "w", kStart);
	stack.Close(stack.Get()[0].Id, kStart + 1s);

	stack.Clear();

	CHECK(stack.Get().empty());
	CHECK_FALSE(stack.NextChange(kStart + 1s).has_value());
}

TEST_CASE("Clearing a group drops only that group's toasts and keeps the rest as they were")
{
	Core::ToastStack stack;
	stack.Show(Core::ToastKind::Error, "Commit failed", "locked", kStart, {}, 1);
	stack.Show(Core::ToastKind::Error, "Theme failed", "bad file", kStart, {}, 0);
	stack.Show(Core::ToastKind::Warning, "Push failed", "rejected", kStart, {}, 1);

	stack.Clear(1);

	REQUIRE(stack.Get().size() == 1);
	CHECK(stack.Get()[0].Title == "Theme failed");
	CHECK(stack.Get()[0].Group == 0);
	CHECK(stack.Get()[0].Count == 1);
}

TEST_CASE("A toast shown with no group is in group zero")
{
	Core::ToastStack stack;
	stack.Show(Core::ToastKind::Error, "Title", "e", kStart);

	REQUIRE(stack.Get().size() == 1);
	CHECK(stack.Get()[0].Group == 0);
}

TEST_CASE("The same message in another group is a toast of its own")
{
	Core::ToastStack stack;
	stack.Show(Core::ToastKind::Error, "Save failed", "access denied", kStart, {}, 0);
	stack.Show(Core::ToastKind::Error, "Save failed", "access denied", kStart, {}, 1);

	REQUIRE(stack.Get().size() == 2);
	CHECK(stack.Get()[0].Count == 1);
	CHECK(stack.Get()[1].Count == 1);

	// The merge still applies within a group.
	stack.Show(Core::ToastKind::Error, "Save failed", "access denied", kStart + 1s, {}, 1);
	REQUIRE(stack.Get().size() == 2);
	CHECK(stack.Get()[1].Count == 2);
}
