#include <doctest/doctest.h>

#include "Editor/Find/MatchColumns.h"

namespace {

	using Matches = std::vector<Core::FindMatch>;

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
