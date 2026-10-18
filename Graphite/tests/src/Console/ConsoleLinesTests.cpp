#include <doctest/doctest.h>

#include "Console/ConsoleLines.h"

#include "ImGuiFrames.h"
#include "SelectionFixtures.h"

#include "Core/Base/Log.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/TextMetrics.h"
#include "Core/Text/TextSelection.h"

#include <cstdint>
#include <optional>
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
}

TEST_CASE("A fresh selection copies nothing")
{
	const Core::TextSelection selection;

	CHECK(Graphite::SelectedConsoleText(selection, ThreeLines()).empty());
}

TEST_CASE("Dragging inside one message takes the characters between the two columns")
{
	CHECK(Graphite::SelectedConsoleText(SelectionBetween({8, 7}, {8, 9}), ThreeLines()) == "to");
}

TEST_CASE("Dragging backwards takes the same characters")
{
	CHECK(Graphite::SelectedConsoleText(SelectionBetween({8, 9}, {8, 7}), ThreeLines()) == "to");
}

TEST_CASE("Dragging across messages takes the tail of the first, all of the middle and the head of the last")
{
	CHECK(Graphite::SelectedConsoleText(SelectionBetween({7, 12}, {9, 2}), ThreeLines()) ==
		  "solution\nfailed to parse\ndo");
}

TEST_CASE("A column past the end of a message stops at the end of it")
{
	CHECK(Graphite::SelectedConsoleText(SelectionBetween({9, 0}, {9, 400}), ThreeLines()) == "done");
}

TEST_CASE("Columns count characters rather than bytes")
{
	const std::vector<Core::LogEntry> entries{Entry(4, "zażółć gęślą")};

	CHECK(Graphite::SelectedConsoleText(SelectionBetween({4, 0}, {4, 6}), entries) == "zażółć");
}

TEST_CASE("A selection across a multi-byte message keeps its characters whole")
{
	const std::vector<Core::LogEntry> entries{Entry(4, "zażółć gęślą"), Entry(5, "ok 😀 done")};

	CHECK(Graphite::SelectedConsoleText(SelectionBetween({4, 3}, {5, 4}), entries) == "ółć gęślą\nok 😀");
}

TEST_CASE("A tab in a message is copied as a tab and counts to the next tab stop")
{
	const std::vector<Core::LogEntry> entries{Entry(3, "a\tbc")};

	CHECK(Graphite::SelectedConsoleText(SelectionBetween({3, 4}, {3, 6}), entries) == "bc");
	CHECK(Graphite::SelectedConsoleText(SelectionBetween({3, 0}, {3, 6}), entries) == "a\tbc");
}

TEST_CASE("A message dropped from the buffer leaves the rest of the selection intact")
{
	const std::vector<Core::LogEntry> remaining{Entry(9, "done")};

	CHECK(Graphite::SelectedConsoleText(SelectionBetween({7, 0}, {9, 4}), remaining) == "done");
}

TEST_CASE("A message the filter hides is not copied out from between two that are shown")
{
	const std::vector<Core::LogEntry> shown{Entry(7, "opening the solution"), Entry(9, "done")};

	CHECK(Graphite::SelectedConsoleText(SelectionBetween({7, 12}, {9, 4}), shown) == "solution\ndone");
}

TEST_CASE("Selecting everything takes every message shown")
{
	CHECK(Graphite::SelectedConsoleText(SelectionBetween({7, 0}, {9, 4}), ThreeLines()) ==
		  "opening the solution\nfailed to parse\ndone");
}

TEST_CASE("A point over a row lands on the character boundary nearest to it")
{
	const Graphite::ConsoleLayout layout{100.0f, 50.0f, 10.0f, 8.0f};

	const std::optional<Core::TextPoint> point = Graphite::PointAt(ThreeLines(), layout, 100.0f + 8.0f * 2.6f,
																   50.0f + 10.0f * 1.5f);

	REQUIRE(point.has_value());
	CHECK(point->Line == 8);
	CHECK(point->Column == 3);
}

TEST_CASE("The character under a point is the one it is over, even on its right half")
{
	const Graphite::ConsoleLayout layout{100.0f, 50.0f, 10.0f, 8.0f};

	const float x = 100.0f + 8.0f * 5.8f;
	const float y = 50.0f + 10.0f * 1.5f;

	REQUIRE(Graphite::PointAt(ThreeLines(), layout, x, y) == Core::TextPoint{8, 6});

	const std::optional<Core::TextPoint> character = Graphite::CharacterAt(ThreeLines(), layout, x, y);

	REQUIRE(character == Core::TextPoint{8, 5});
	CHECK(Graphite::ConsoleWordAt(ThreeLines(), *character) == Core::ColumnSpan{0, 6});
}

TEST_CASE("The character under a point is clamped like the point")
{
	const Graphite::ConsoleLayout layout{0.0f, 0.0f, 10.0f, 8.0f};

	CHECK(Graphite::CharacterAt(ThreeLines(), layout, 4000.0f, 25.0f) == Core::TextPoint{9, 4});
	CHECK(Graphite::CharacterAt(ThreeLines(), layout, -30.0f, 5.0f) == Core::TextPoint{7, 0});
	CHECK(Graphite::CharacterAt(ThreeLines(), layout, 30.0f, -200.0f) == Core::TextPoint{7, 0});
	CHECK_FALSE(Graphite::CharacterAt({}, layout, 10.0f, 10.0f).has_value());
}

TEST_CASE("A point past the end of a row lands at the end of it")
{
	const Graphite::ConsoleLayout layout{0.0f, 0.0f, 10.0f, 8.0f};

	const std::optional<Core::TextPoint> point = Graphite::PointAt(ThreeLines(), layout, 4000.0f, 25.0f);

	REQUIRE(point.has_value());
	CHECK(point->Line == 9);
	CHECK(point->Column == 4);
}

TEST_CASE("A point left of the rows lands at the start of the row")
{
	const Graphite::ConsoleLayout layout{100.0f, 0.0f, 10.0f, 8.0f};

	const std::optional<Core::TextPoint> point = Graphite::PointAt(ThreeLines(), layout, 0.0f, 5.0f);

	REQUIRE(point.has_value());
	CHECK(point->Line == 7);
	CHECK(point->Column == 0);
}

TEST_CASE("Dragging above the rows lands at the start of the first message")
{
	const Graphite::ConsoleLayout layout{0.0f, 50.0f, 10.0f, 8.0f};

	const std::optional<Core::TextPoint> point = Graphite::PointAt(ThreeLines(), layout, 30.0f, -200.0f);

	REQUIRE(point.has_value());
	CHECK(point->Line == 7);
	CHECK(point->Column == 0);
}

TEST_CASE("Dragging below the rows lands at the end of the last message")
{
	const Graphite::ConsoleLayout layout{0.0f, 0.0f, 10.0f, 8.0f};

	const std::optional<Core::TextPoint> point = Graphite::PointAt(ThreeLines(), layout, 0.0f, 900.0f);

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

	const std::optional<Core::TextPoint> point = Graphite::PointAt(entries, layout, 4000.0f, 5.0f);

	REQUIRE(point.has_value());
	CHECK(point->Column == 6);
}

TEST_CASE("A double-click on a word selects the word")
{
	const Core::ColumnSpan span = Graphite::ConsoleWordAt(ThreeLines(), {8, 1});

	CHECK(span == Core::ColumnSpan{0, 6});
}

TEST_CASE("A word after a tab is returned in expanded columns")
{
	const std::vector<Core::LogEntry> entries{Entry(3, "a\tword")};

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

TEST_CASE("The length of a line is its expanded columns, and a line that is not shown has none")
{
	const std::vector<Core::LogEntry> entries{Entry(3, "a\tbc")};

	CHECK(Graphite::ConsoleLineLength(entries, 3) == 6);
	CHECK(Graphite::ConsoleLineLength(entries, 4) == 0);
}

TEST_CASE("Everything shown runs from the start of the first message to the end of the last")
{
	const auto [start, end] = Graphite::EverythingShown(ThreeLines());

	CHECK(start == Core::TextPoint{7, 0});
	CHECK(end == Core::TextPoint{9, 4});
	CHECK(Graphite::EverythingShown({}) == std::pair<Core::TextPoint, Core::TextPoint>{});
}

TEST_CASE("The console measures its cells and draws its lines at the code size")
{
	ImGuiFrames frames;

	float codeCell = 0.0f;
	float defaultCell = 0.0f;
	float measuredCell = 0.0f;
	float codeLineHeight = 0.0f;
	float measuredRowHeight = 0.0f;

	for (int frame = 0; frame < 2; ++frame)
	{
		frames.Next([&] {
			ImGui::Begin("console");

			defaultCell = Core::MonospaceCellWidth();

			Core::PushCodeFont();
			codeCell = Core::MonospaceCellWidth();
			codeLineHeight = ImGui::GetTextLineHeight();
			ImGui::PopFont();

			const Graphite::ConsoleLayout layout = Graphite::MeasureConsoleLayout(0.0f);
			measuredCell = layout.CellWidth;
			measuredRowHeight = layout.RowHeight;

			ImGui::End();
		});
	}

	CHECK(codeCell != defaultCell);
	CHECK(measuredCell == doctest::Approx(codeCell));
	CHECK(measuredRowHeight == doctest::Approx(codeLineHeight));
}
