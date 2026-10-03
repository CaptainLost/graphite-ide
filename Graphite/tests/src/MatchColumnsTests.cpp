#include <doctest/doctest.h>

#include "Editor/Find/MatchColumns.h"

namespace {

	using Matches = std::vector<std::pair<std::size_t, std::size_t>>;

	// The line under test always starts at byte 100, so a document offset in these cases is
	// visibly not a line offset and an accidental confusion of the two shows up as a wild column
	// rather than as an off-by-a-little that still looks plausible.
	constexpr std::size_t LineStart = 100;

	Graphite::LineMatches ColumnsOf(std::string_view lineText, const Matches& matches,
									std::size_t currentIndex, std::size_t tabWidth = 4)
	{
		Graphite::LineMatchInput input{matches};

		input.LineText = lineText;
		input.LineStart = LineStart;
		input.CurrentMatch = currentIndex;
		input.TabWidth = tabWidth;

		return Graphite::MatchColumnsOnLine(input);
	}
}

TEST_CASE("A match inside the line becomes the columns it covers")
{
	const Graphite::LineMatches result = ColumnsOf("abcdef", {{LineStart + 2, LineStart + 4}}, 0);

	REQUIRE(result.Columns.size() == 1);
	CHECK(result.Columns[0].FromColumn == 2);
	CHECK(result.Columns[0].ToColumn == 4);
}

TEST_CASE("A match running in from the line above starts at the line's first column")
{
	// Begins 5 bytes before the line and ends 3 bytes into it.
	const Graphite::LineMatches result = ColumnsOf("abcdef", {{LineStart - 5, LineStart + 3}}, 0);

	REQUIRE(result.Columns.size() == 1);
	CHECK(result.Columns[0].FromColumn == 0);
	CHECK(result.Columns[0].ToColumn == 3);
}

TEST_CASE("A match running on into the line below stops at the line's last column")
{
	const Graphite::LineMatches result = ColumnsOf("abcdef", {{LineStart + 4, LineStart + 20}}, 0);

	REQUIRE(result.Columns.size() == 1);
	CHECK(result.Columns[0].FromColumn == 4);
	CHECK(result.Columns[0].ToColumn == 6);
}

TEST_CASE("A match ending exactly at the line's start belongs to the line above")
{
	const Graphite::LineMatches result = ColumnsOf("abcdef", {{LineStart - 3, LineStart}}, 0);

	CHECK(result.Columns.empty());
}

TEST_CASE("A match starting exactly at the line's end is kept")
{
	// The newline's own offset: the match begins where this line stops and runs into the next.
	// Half-open the other way round would drop it here and again on the line below.
	const Graphite::LineMatches result = ColumnsOf("abcdef", {{LineStart + 6, LineStart + 9}}, 0);

	REQUIRE(result.Columns.size() == 1);
	CHECK(result.Columns[0].FromColumn == 6);
	CHECK(result.Columns[0].ToColumn == 6);
}

TEST_CASE("Two matches on one line are both converted, in order")
{
	const Graphite::LineMatches result = ColumnsOf(
		"abcdefghij", {{LineStart + 1, LineStart + 3}, {LineStart + 6, LineStart + 8}}, 0);

	REQUIRE(result.Columns.size() == 2);
	CHECK(result.Columns[0].FromColumn == 1);
	CHECK(result.Columns[0].ToColumn == 3);
	CHECK(result.Columns[1].FromColumn == 6);
	CHECK(result.Columns[1].ToColumn == 8);
}

TEST_CASE("A line whose matches are all ordinary reports no current match")
{
	// The caret is on match 2, which is on some later line. Current must be past the end of
	// this line's own list — zero would paint this line's first match as the current one.
	const Matches matches = {{LineStart + 1, LineStart + 3},
							 {LineStart + 6, LineStart + 8},
							 {LineStart + 400, LineStart + 402}};

	const Graphite::LineMatches result = ColumnsOf("abcdefghij", matches, 2);

	REQUIRE(result.Columns.size() == 2);
	CHECK(result.Current == 2);
	CHECK(result.Current == result.Columns.size());
}

TEST_CASE("The current match is reported by its index within the line, not within the document")
{
	// Match 3 of the document is the second of the two on this line.
	const Matches matches = {{LineStart - 40, LineStart - 38},
							 {LineStart - 20, LineStart - 18},
							 {LineStart + 1, LineStart + 3},
							 {LineStart + 6, LineStart + 8}};

	const Graphite::LineMatches result = ColumnsOf("abcdefghij", matches, 3);

	REQUIRE(result.Columns.size() == 2);
	CHECK(result.Current == 1);
}

TEST_CASE("A leading tab moves the columns by the tab width, which is why this conversion exists")
{
	// One tab then "abc", with the match on "b" — byte 2 to 3 of the line either way. At tab
	// width 4 the tab occupies columns 0-3, at width 8 it occupies 0-7.
	const Matches matches = {{LineStart + 2, LineStart + 3}};

	const Graphite::LineMatches narrow = ColumnsOf("\tabc", matches, 0, 4);

	REQUIRE(narrow.Columns.size() == 1);
	CHECK(narrow.Columns[0].FromColumn == 5);
	CHECK(narrow.Columns[0].ToColumn == 6);

	const Graphite::LineMatches wide = ColumnsOf("\tabc", matches, 0, 8);

	REQUIRE(wide.Columns.size() == 1);
	CHECK(wide.Columns[0].FromColumn == 9);
	CHECK(wide.Columns[0].ToColumn == 10);
}
