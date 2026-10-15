#include <doctest/doctest.h>

#include "Git/History/HistoryStatus.h"

#include <optional>

using Graphite::DescribeHistory;
using Graphite::HistoryActions;
using Graphite::HistoryStatus;
using Graphite::HistoryStatusInput;

TEST_CASE("An unfinished history with its length known says how much of it is shown and offers to load more")
{
	const std::optional<HistoryStatus> status =
		DescribeHistory(HistoryStatusInput{.Shown = 500, .Walked = 500, .Complete = false, .Total = 1739});

	REQUIRE(status.has_value());
	CHECK(status->Text == "500 of 1,739 commits");
	CHECK(status->Actions == HistoryActions::Load);
	CHECK(status->Busy.empty());
}

TEST_CASE("Until the history's length is counted the line says it is counting")
{
	const std::optional<HistoryStatus> status =
		DescribeHistory(HistoryStatusInput{.Shown = 500, .Walked = 500, .Complete = false, .Counting = true});

	REQUIRE(status.has_value());
	CHECK(status->Text == "500 commits · counting…");
	CHECK(status->Actions == HistoryActions::Load);
}

TEST_CASE("A count that failed leaves the line without a length and without counting")
{
	const std::optional<HistoryStatus> status =
		DescribeHistory(HistoryStatusInput{.Shown = 500, .Walked = 500, .Complete = false});

	REQUIRE(status.has_value());
	CHECK(status->Text == "500 commits");
	CHECK(status->Actions == HistoryActions::Load);
}

TEST_CASE("A history read to its root says all of it is shown and offers nothing")
{
	const std::optional<HistoryStatus> status =
		DescribeHistory(HistoryStatusInput{.Shown = 1739, .Walked = 1739, .Complete = true, .Total = 1739});

	REQUIRE(status.has_value());
	CHECK(status->Text == "All 1,739 commits");
	CHECK(status->Actions == HistoryActions::None);
}

TEST_CASE("A history of one commit reads in the singular")
{
	const std::optional<HistoryStatus> status =
		DescribeHistory(HistoryStatusInput{.Shown = 1, .Walked = 1, .Complete = true, .Total = 1});

	REQUIRE(status.has_value());
	CHECK(status->Text == "1 commit");
}

TEST_CASE("A filtered search stopped early says how far it looked and offers to search further")
{
	const std::optional<HistoryStatus> status = DescribeHistory(HistoryStatusInput{
		.Shown = 37, .Walked = 20000, .Complete = false, .Filtered = true, .Total = 50000});

	REQUIRE(status.has_value());
	CHECK(status->Text == "37 matching commits in the newest 20,000 of 50,000");
	CHECK(status->Actions == HistoryActions::Search);
}

TEST_CASE("A filtered search stopped early before the length is known says only how far it looked")
{
	const std::optional<HistoryStatus> status = DescribeHistory(HistoryStatusInput{
		.Shown = 37, .Walked = 20000, .Complete = false, .Filtered = true, .Counting = true});

	REQUIRE(status.has_value());
	CHECK(status->Text == "37 matching commits in the newest 20,000");
	CHECK(status->Actions == HistoryActions::Search);
}

TEST_CASE("A filtered search that reached the root says it looked through all of it")
{
	const std::optional<HistoryStatus> status = DescribeHistory(HistoryStatusInput{
		.Shown = 58, .Walked = 50000, .Complete = true, .Filtered = true, .Total = 50000});

	REQUIRE(status.has_value());
	CHECK(status->Text == "58 matching commits in all 50,000");
	CHECK(status->Actions == HistoryActions::None);
}

TEST_CASE("A filter that matches nothing still says how far it looked and offers to search further")
{
	const std::optional<HistoryStatus> status = DescribeHistory(HistoryStatusInput{
		.Shown = 0, .Walked = 20000, .Complete = false, .Filtered = true, .Total = 50000});

	REQUIRE(status.has_value());
	CHECK(status->Text == "0 matching commits in the newest 20,000 of 50,000");
	CHECK(status->Actions == HistoryActions::Search);
}

TEST_CASE("One match reads in the singular")
{
	const std::optional<HistoryStatus> status = DescribeHistory(HistoryStatusInput{
		.Shown = 1, .Walked = 300, .Complete = true, .Filtered = true, .Total = 300});

	REQUIRE(status.has_value());
	CHECK(status->Text == "1 matching commit in all 300");
}

TEST_CASE("While a query the reader asked for runs the line says Loading in place of the buttons")
{
	const std::optional<HistoryStatus> status = DescribeHistory(
		HistoryStatusInput{.Shown = 500, .Walked = 500, .Complete = false, .Total = 1739, .Asking = true});

	REQUIRE(status.has_value());
	CHECK(status->Text == "500 of 1,739 commits");
	CHECK(status->Busy == "Loading…");
	CHECK(status->Actions == HistoryActions::None);
}

TEST_CASE("While a filtered query the reader asked for runs the line says Searching in place of the buttons")
{
	const std::optional<HistoryStatus> status = DescribeHistory(HistoryStatusInput{
		.Shown = 37, .Walked = 20000, .Complete = false, .Filtered = true, .Total = 50000, .Asking = true});

	REQUIRE(status.has_value());
	CHECK(status->Busy == "Searching…");
	CHECK(status->Actions == HistoryActions::None);
}

TEST_CASE("A stash shows no line")
{
	CHECK_FALSE(DescribeHistory(HistoryStatusInput{.Shown = 1, .Walked = 1, .Complete = true, .Stash = true}).has_value());
}

TEST_CASE("An empty history without a filter shows no line")
{
	CHECK_FALSE(DescribeHistory(HistoryStatusInput{.Shown = 0, .Walked = 0, .Complete = true}).has_value());
}
