#include <doctest/doctest.h>

#include "Git/GenerationSlot.h"

#include <string>

using Graphite::GenerationSlot;
using Graphite::SlotLanding;

namespace {

	Core::Result<std::string> Answer(const char* text)
	{
		return std::string(text);
	}

	Core::Result<std::string> Failure(const char* message)
	{
		return Core::Error{Core::ErrorCode::IoFailure, message};
	}

	// WARNING: Stated as a bool so a failed check prints rather than trying to stream a pointer.
	bool IsEmpty(const GenerationSlot<std::string>& slot)
	{
		return slot.Get() == nullptr;
	}
}

TEST_CASE("A slot holds nothing until its first answer lands")
{
	GenerationSlot<std::string> slot;

	CHECK(IsEmpty(slot));
	CHECK_FALSE(slot.GetError().has_value());
	CHECK_FALSE(slot.IsInFlight());
}

TEST_CASE("A slot is in flight from a request until that request's answer lands")
{
	GenerationSlot<std::string> slot;

	const std::uint64_t generation = slot.Begin();
	CHECK(slot.IsInFlight());
	CHECK(slot.GetRequested() == generation);

	CHECK(slot.Land(generation, Answer("one")) == SlotLanding::Applied);
	CHECK_FALSE(slot.IsInFlight());
	CHECK(slot.GetApplied() == generation);
	CHECK(*slot.Get() == "one");
}

TEST_CASE("A slot drops an answer older than one already applied")
{
	GenerationSlot<std::string> slot;

	const std::uint64_t older = slot.Begin();
	const std::uint64_t newer = slot.Begin();

	CHECK(slot.Land(newer, Answer("newer")) == SlotLanding::Applied);
	CHECK(slot.Land(older, Answer("older")) == SlotLanding::Stale);
	CHECK(slot.Land(older, Failure("older")) == SlotLanding::Stale);

	CHECK(*slot.Get() == "newer");
	CHECK_FALSE(slot.GetError().has_value());
}

TEST_CASE("A failure keeps the last good answer, and the next success clears the error")
{
	GenerationSlot<std::string> slot;

	REQUIRE(slot.Land(slot.Begin(), Answer("good")) == SlotLanding::Applied);

	CHECK(slot.Land(slot.Begin(), Failure("broken")) == SlotLanding::Failed);
	CHECK(*slot.Get() == "good");
	REQUIRE(slot.GetError().has_value());
	CHECK(slot.GetError()->Message == "broken");

	CHECK(slot.Land(slot.Begin(), Answer("better")) == SlotLanding::Applied);
	CHECK(*slot.Get() == "better");
	CHECK_FALSE(slot.GetError().has_value());
}

TEST_CASE("An answer equal to the held one keeps the held pointer and clears the error")
{
	GenerationSlot<std::string> slot;

	REQUIRE(slot.Land(slot.Begin(), Answer("same")) == SlotLanding::Applied);
	const Core::Ref<const std::string> held = slot.Get();
	REQUIRE(slot.Land(slot.Begin(), Failure("broken")) == SlotLanding::Failed);

	const std::uint64_t generation = slot.Begin();

	CHECK(slot.LandSameAsHeld(generation) == SlotLanding::Applied);
	const bool keptThePointer = slot.Get() == held;
	CHECK(keptThePointer);
	CHECK_FALSE(slot.GetError().has_value());
	CHECK_FALSE(slot.IsInFlight());
	CHECK(slot.LandSameAsHeld(generation) == SlotLanding::Stale);
}

TEST_CASE("Clearing a slot forgets its answer and drops one still in flight")
{
	GenerationSlot<std::string> slot;

	REQUIRE(slot.Land(slot.Begin(), Failure("broken")) == SlotLanding::Failed);
	const std::uint64_t inFlight = slot.Begin();

	slot.Clear();

	CHECK(IsEmpty(slot));
	CHECK_FALSE(slot.GetError().has_value());
	CHECK_FALSE(slot.IsInFlight());
	CHECK(slot.Land(inFlight, Answer("late")) == SlotLanding::Stale);
	CHECK(IsEmpty(slot));

	const std::uint64_t next = slot.Begin();
	CHECK(next > inFlight);
	CHECK(slot.Land(next, Answer("fresh")) == SlotLanding::Applied);
	CHECK(*slot.Get() == "fresh");
}
