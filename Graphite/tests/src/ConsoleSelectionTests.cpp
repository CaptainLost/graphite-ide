#include <doctest/doctest.h>

#include "Panels/ConsoleSelection.h"

#include "Core/Base/Log.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

	Core::LogEntry Entry(std::uint64_t sequence, std::string message)
	{
		Core::LogEntry entry;
		entry.Message = std::move(message);
		entry.Sequence = sequence;
		return entry;
	}

	std::vector<Core::LogEntry> ThreeLines()
	{
		return {Entry(7, "opening the solution"), Entry(8, "failed to parse"),
				Entry(9, "done")};
	}
}

TEST_CASE("A fresh selection holds nothing")
{
	const Graphite::ConsoleSelection selection;

	CHECK(selection.IsEmpty());
	CHECK(selection.ExtractText(ThreeLines()).empty());
}

TEST_CASE("Pressing without dragging selects nothing")
{
	Graphite::ConsoleSelection selection;

	selection.Begin({8, 3});

	CHECK(selection.IsEmpty());
}

TEST_CASE("Dragging inside one message takes the characters between the two columns")
{
	Graphite::ConsoleSelection selection;

	selection.Begin({8, 7});
	selection.ExtendTo({8, 9});

	CHECK(selection.ExtractText(ThreeLines()) == "to");
}

TEST_CASE("Dragging backwards takes the same characters")
{
	Graphite::ConsoleSelection selection;

	selection.Begin({8, 9});
	selection.ExtendTo({8, 7});

	CHECK(selection.ExtractText(ThreeLines()) == "to");
}

TEST_CASE("Dragging across messages takes the tail of the first, all of the middle and the head of the last")
{
	Graphite::ConsoleSelection selection;

	selection.Begin({7, 12});
	selection.ExtendTo({9, 2});

	CHECK(selection.ExtractText(ThreeLines()) == "solution\nfailed to parse\ndo");
}

TEST_CASE("A column past the end of a message stops at the end of it")
{
	Graphite::ConsoleSelection selection;

	selection.Begin({9, 0});
	selection.ExtendTo({9, 400});

	CHECK(selection.ExtractText(ThreeLines()) == "done");
}

TEST_CASE("Columns count characters rather than bytes")
{
	const std::vector<Core::LogEntry> entries{Entry(4, "zażółć gęślą")};

	Graphite::ConsoleSelection selection;
	selection.Begin({4, 0});
	selection.ExtendTo({4, 6});

	CHECK(selection.ExtractText(entries) == "zażółć");
}

TEST_CASE("A message dropped from the buffer leaves the rest of the selection intact")
{
	Graphite::ConsoleSelection selection;

	selection.Begin({7, 0});
	selection.ExtendTo({9, 4});

	// The buffer has since dropped the two oldest messages, so the selection names one
	// message that is no longer there.
	const std::vector<Core::LogEntry> remaining{Entry(9, "done")};

	CHECK(selection.ExtractText(remaining) == "done");
}

TEST_CASE("A message the filter hides is not copied out from between two that are shown")
{
	Graphite::ConsoleSelection selection;

	selection.Begin({7, 12});
	selection.ExtendTo({9, 4});

	const std::vector<Core::LogEntry> shown{Entry(7, "opening the solution"), Entry(9, "done")};

	CHECK(selection.ExtractText(shown) == "solution\ndone");
}

TEST_CASE("Selecting all takes every message shown")
{
	Graphite::ConsoleSelection selection;

	selection.SelectAll(ThreeLines());

	CHECK(selection.ExtractText(ThreeLines()) == "opening the solution\nfailed to parse\ndone");
}

TEST_CASE("Selecting all with nothing shown selects nothing")
{
	Graphite::ConsoleSelection selection;

	selection.SelectAll({});

	CHECK(selection.IsEmpty());
}

TEST_CASE("A message outside the selection is not highlighted")
{
	Graphite::ConsoleSelection selection;

	selection.Begin({8, 0});
	selection.ExtendTo({8, 4});

	CHECK_FALSE(selection.ColumnsOn(7, 20).has_value());
	CHECK_FALSE(selection.ColumnsOn(9, 4).has_value());
}

TEST_CASE("The messages between the two ends are highlighted whole")
{
	Graphite::ConsoleSelection selection;

	selection.Begin({7, 12});
	selection.ExtendTo({9, 2});

	const auto first = selection.ColumnsOn(7, 20);
	REQUIRE(first.has_value());
	CHECK(first->From == 12);
	CHECK(first->To == 20);

	const auto middle = selection.ColumnsOn(8, 15);
	REQUIRE(middle.has_value());
	CHECK(middle->From == 0);
	CHECK(middle->To == 15);

	const auto last = selection.ColumnsOn(9, 4);
	REQUIRE(last.has_value());
	CHECK(last->From == 0);
	CHECK(last->To == 2);
}

TEST_CASE("Clearing puts the selection back to holding nothing")
{
	Graphite::ConsoleSelection selection;

	selection.Begin({7, 0});
	selection.ExtendTo({9, 4});
	selection.Clear();

	CHECK(selection.IsEmpty());
	CHECK_FALSE(selection.ColumnsOn(8, 15).has_value());
}

TEST_CASE("A point over a row lands on the character boundary nearest to it")
{
	const Graphite::ConsoleLayout layout{100.0f, 50.0f, 10.0f, 8.0f};

	// Second row, a little past the middle of the third character.
	const auto anchor = Graphite::AnchorAtPoint(ThreeLines(), layout, 100.0f + 8.0f * 2.6f,
												50.0f + 10.0f * 1.5f);

	REQUIRE(anchor.has_value());
	CHECK(anchor->Sequence == 8);
	CHECK(anchor->Column == 3);
}

TEST_CASE("A point past the end of a row lands at the end of it")
{
	const Graphite::ConsoleLayout layout{0.0f, 0.0f, 10.0f, 8.0f};

	const auto anchor = Graphite::AnchorAtPoint(ThreeLines(), layout, 4000.0f, 25.0f);

	REQUIRE(anchor.has_value());
	CHECK(anchor->Sequence == 9);
	CHECK(anchor->Column == 4);
}

TEST_CASE("A point left of the rows lands at the start of the row")
{
	const Graphite::ConsoleLayout layout{100.0f, 0.0f, 10.0f, 8.0f};

	const auto anchor = Graphite::AnchorAtPoint(ThreeLines(), layout, 0.0f, 5.0f);

	REQUIRE(anchor.has_value());
	CHECK(anchor->Sequence == 7);
	CHECK(anchor->Column == 0);
}

TEST_CASE("Dragging above the rows lands at the start of the first message")
{
	const Graphite::ConsoleLayout layout{0.0f, 50.0f, 10.0f, 8.0f};

	const auto anchor = Graphite::AnchorAtPoint(ThreeLines(), layout, 30.0f, -200.0f);

	REQUIRE(anchor.has_value());
	CHECK(anchor->Sequence == 7);
	CHECK(anchor->Column == 0);
}

TEST_CASE("Dragging below the rows lands at the end of the last message")
{
	const Graphite::ConsoleLayout layout{0.0f, 0.0f, 10.0f, 8.0f};

	const auto anchor = Graphite::AnchorAtPoint(ThreeLines(), layout, 0.0f, 900.0f);

	REQUIRE(anchor.has_value());
	CHECK(anchor->Sequence == 9);
	CHECK(anchor->Column == 4);
}

TEST_CASE("An empty console has nowhere to land")
{
	const Graphite::ConsoleLayout layout{0.0f, 0.0f, 10.0f, 8.0f};

	CHECK_FALSE(Graphite::AnchorAtPoint({}, layout, 10.0f, 10.0f).has_value());
}
