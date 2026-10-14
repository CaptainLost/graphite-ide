#include <doctest/doctest.h>

#include "Git/History/CommitColumns.h"

namespace {
	Graphite::CommitColumnsInput InputAt(float available, float graph = 30.0f)
	{
		Graphite::CommitColumnsInput input;
		input.Available = available;
		input.Graph = graph;
		input.Chevron = 20.0f;
		input.Hash = 60.0f;
		input.Author = 110.0f;
		input.Date = 90.0f;
		input.CellPadding = 8.0f;
		input.MinMessage = 84.0f;
		return input;
	}
}

TEST_CASE("A wide table shows every column and gives the rest to the message")
{
	const Graphite::CommitColumns columns = Graphite::LayoutCommitColumns(InputAt(900.0f));

	CHECK(columns.ShowAuthor);
	CHECK(columns.ShowDate);
	CHECK(columns.Graph == 30.0f);
	CHECK(columns.Message == doctest::Approx(900.0f - (30.0f + 20.0f + 60.0f + 110.0f + 90.0f + 8.0f * 6.0f)));
}

TEST_CASE("The graph is capped to a quarter of the table")
{
	const Graphite::CommitColumns columns = Graphite::LayoutCommitColumns(InputAt(400.0f, 600.0f));

	CHECK(columns.Graph == doctest::Approx(100.0f));
}

TEST_CASE("The author is given up first, then the date")
{
	const Graphite::CommitColumns noAuthor = Graphite::LayoutCommitColumns(InputAt(420.0f));

	CHECK_FALSE(noAuthor.ShowAuthor);
	CHECK(noAuthor.ShowDate);
	CHECK(noAuthor.Message >= 84.0f);

	const Graphite::CommitColumns neither = Graphite::LayoutCommitColumns(InputAt(300.0f));

	CHECK_FALSE(neither.ShowAuthor);
	CHECK_FALSE(neither.ShowDate);
	CHECK(neither.Message >= 84.0f);
}

TEST_CASE("A table narrow enough to leave the message nothing today gives it its minimum")
{
	const Graphite::CommitColumnsInput input = InputAt(300.0f, 400.0f);
	const float today = input.Available - (input.Graph + input.Chevron + input.Hash + input.Author +
										   input.Date + input.CellPadding * 6.0f);
	REQUIRE(today <= 0.0f);

	const Graphite::CommitColumns columns = Graphite::LayoutCommitColumns(input);

	CHECK(columns.Message >= input.MinMessage);
}

TEST_CASE("When even the always-shown columns leave less than the minimum the message gets what is left")
{
	const Graphite::CommitColumns columns = Graphite::LayoutCommitColumns(InputAt(100.0f));

	CHECK_FALSE(columns.ShowAuthor);
	CHECK_FALSE(columns.ShowDate);
	CHECK(columns.Message < 84.0f);
}