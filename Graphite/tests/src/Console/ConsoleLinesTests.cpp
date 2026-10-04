#include <doctest/doctest.h>

#include "Console/ConsoleLines.h"

#include "Core/Base/Log.h"
#include "Core/Text/TextSelection.h"

#include <cstdint>
#include <string>
#include <utility>
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

	Core::TextSelection Between(Core::TextPoint from, Core::TextPoint to)
	{
		Core::TextSelection selection;
		selection.Begin(from);
		selection.ExtendTo(to);
		return selection;
	}
}

TEST_CASE("A fresh selection copies nothing")
{
	const Core::TextSelection selection;

	CHECK(Graphite::SelectedConsoleText(selection, ThreeLines()).empty());
}

TEST_CASE("Dragging inside one message takes the characters between the two columns")
{
	CHECK(Graphite::SelectedConsoleText(Between({8, 7}, {8, 9}), ThreeLines()) == "to");
}

TEST_CASE("Dragging backwards takes the same characters")
{
	CHECK(Graphite::SelectedConsoleText(Between({8, 9}, {8, 7}), ThreeLines()) == "to");
}

TEST_CASE("Dragging across messages takes the tail of the first, all of the middle and the head of the last")
{
	CHECK(Graphite::SelectedConsoleText(Between({7, 12}, {9, 2}), ThreeLines()) ==
		  "solution\nfailed to parse\ndo");
}

TEST_CASE("A column past the end of a message stops at the end of it")
{
	CHECK(Graphite::SelectedConsoleText(Between({9, 0}, {9, 400}), ThreeLines()) == "done");
}

TEST_CASE("Columns count characters rather than bytes")
{
	const std::vector<Core::LogEntry> entries{Entry(4, "zażółć gęślą")};

	CHECK(Graphite::SelectedConsoleText(Between({4, 0}, {4, 6}), entries) == "zażółć");
}

TEST_CASE("A selection across a multi-byte message keeps its characters whole")
{
	const std::vector<Core::LogEntry> entries{Entry(4, "zażółć gęślą"), Entry(5, "ok 😀 done")};

	// Column 3 is between "zaż" and "ółć"; column 4 of the next message is after the emoji.
	CHECK(Graphite::SelectedConsoleText(Between({4, 3}, {5, 4}), entries) == "ółć gęślą\nok 😀");
}

TEST_CASE("A tab in a message is copied as a tab and counts to the next tab stop")
{
	const std::vector<Core::LogEntry> entries{Entry(3, "a\tbc")};

	// Columns: a = 0, the tab fills 1..4, b = 4, c = 5.
	CHECK(Graphite::SelectedConsoleText(Between({3, 4}, {3, 6}), entries) == "bc");
	CHECK(Graphite::SelectedConsoleText(Between({3, 0}, {3, 6}), entries) == "a\tbc");
}

TEST_CASE("A message dropped from the buffer leaves the rest of the selection intact")
{
	// The buffer has since dropped the two oldest messages, so the selection names one
	// message that is no longer there.
	const std::vector<Core::LogEntry> remaining{Entry(9, "done")};

	CHECK(Graphite::SelectedConsoleText(Between({7, 0}, {9, 4}), remaining) == "done");
}

TEST_CASE("A message the filter hides is not copied out from between two that are shown")
{
	const std::vector<Core::LogEntry> shown{Entry(7, "opening the solution"), Entry(9, "done")};

	CHECK(Graphite::SelectedConsoleText(Between({7, 12}, {9, 4}), shown) == "solution\ndone");
}

TEST_CASE("Selecting everything takes every message shown")
{
	CHECK(Graphite::SelectedConsoleText(Between({7, 0}, {9, 4}), ThreeLines()) ==
		  "opening the solution\nfailed to parse\ndone");
}

TEST_CASE("A point over a row lands on the character boundary nearest to it")
{
	const Graphite::ConsoleLayout layout{100.0f, 50.0f, 10.0f, 8.0f};

	// Second row, a little past the middle of the third character.
	const auto point = Graphite::PointAt(ThreeLines(), layout, 100.0f + 8.0f * 2.6f,
										 50.0f + 10.0f * 1.5f);

	REQUIRE(point.has_value());
	CHECK(point->Line == 8);
	CHECK(point->Column == 3);
}

TEST_CASE("A point past the end of a row lands at the end of it")
{
	const Graphite::ConsoleLayout layout{0.0f, 0.0f, 10.0f, 8.0f};

	const auto point = Graphite::PointAt(ThreeLines(), layout, 4000.0f, 25.0f);

	REQUIRE(point.has_value());
	CHECK(point->Line == 9);
	CHECK(point->Column == 4);
}

TEST_CASE("A point left of the rows lands at the start of the row")
{
	const Graphite::ConsoleLayout layout{100.0f, 0.0f, 10.0f, 8.0f};

	const auto point = Graphite::PointAt(ThreeLines(), layout, 0.0f, 5.0f);

	REQUIRE(point.has_value());
	CHECK(point->Line == 7);
	CHECK(point->Column == 0);
}

TEST_CASE("Dragging above the rows lands at the start of the first message")
{
	const Graphite::ConsoleLayout layout{0.0f, 50.0f, 10.0f, 8.0f};

	const auto point = Graphite::PointAt(ThreeLines(), layout, 30.0f, -200.0f);

	REQUIRE(point.has_value());
	CHECK(point->Line == 7);
	CHECK(point->Column == 0);
}

TEST_CASE("Dragging below the rows lands at the end of the last message")
{
	const Graphite::ConsoleLayout layout{0.0f, 0.0f, 10.0f, 8.0f};

	const auto point = Graphite::PointAt(ThreeLines(), layout, 0.0f, 900.0f);

	REQUIRE(point.has_value());
	CHECK(point->Line == 9);
	CHECK(point->Column == 4);
}

TEST_CASE("An empty console has nowhere to land")
{
	const Graphite::ConsoleLayout layout{0.0f, 0.0f, 10.0f, 8.0f};

	CHECK_FALSE(Graphite::PointAt({}, layout, 10.0f, 10.0f).has_value());
}

TEST_CASE("A column on a message with a tab is counted in expanded columns")
{
	const std::vector<Core::LogEntry> entries{Entry(3, "a\tbc")};
	const Graphite::ConsoleLayout layout{0.0f, 0.0f, 10.0f, 8.0f};

	// Six columns in all; the pointer well to the right lands at the end of them.
	const auto point = Graphite::PointAt(entries, layout, 4000.0f, 5.0f);

	REQUIRE(point.has_value());
	CHECK(point->Column == 6);
}

TEST_CASE("A double-click on a word selects the word")
{
	const auto span = Graphite::ConsoleWordAt(ThreeLines(), {8, 1});

	CHECK(span == Core::ColumnSpan{0, 6});
}

TEST_CASE("A word after a tab is returned in expanded columns")
{
	const std::vector<Core::LogEntry> entries{Entry(3, "a\tword")};

	// "word" sits at columns 4..8 once the tab is expanded, though it starts at byte 2.
	CHECK(Graphite::ConsoleWordAt(entries, {3, 5}) == Core::ColumnSpan{4, 8});
}

TEST_CASE("A word after multi-byte characters is returned in characters, not bytes")
{
	const std::vector<Core::LogEntry> entries{Entry(4, "zażółć gęślą")};

	CHECK(Graphite::ConsoleWordAt(entries, {4, 9}) == Core::ColumnSpan{7, 12});
}

TEST_CASE("A double-click on an entry that is not shown selects nothing")
{
	const Core::ColumnSpan span = Graphite::ConsoleWordAt(ThreeLines(), {99, 3});

	CHECK(span.From == span.To);
}
