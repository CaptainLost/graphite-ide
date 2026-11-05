#include <doctest/doctest.h>

#include "Git/History/HistoryStatus.h"

#include <optional>

using Graphite::DescribeHistory;
using Graphite::HistoryActions;
using Graphite::HistoryStatus;
using Graphite::HistoryStatusInput;

TEST_CASE("Git window: the Merges history says how many rows are loaded and what a row is")
{
	const std::optional<HistoryStatus> status =
		DescribeHistory(HistoryStatusInput{.Shown = 3448, .Complete = true, .FirstParentsOnly = true});

	REQUIRE(status.has_value());
	CHECK(status->Text == "3,448 rows · first parents only: each merge, squash or direct commit is one row");
	CHECK(status->Actions == HistoryActions::None);
	CHECK(status->Busy.empty());
}

TEST_CASE("Git window: the All commits history says how many commits are loaded")
{
	const std::optional<HistoryStatus> status =
		DescribeHistory(HistoryStatusInput{.Shown = 500, .Complete = false, .FirstParentsOnly = false});

	REQUIRE(status.has_value());
	CHECK(status->Text == "500 commits · every commit with its graph");
	CHECK(status->Actions == HistoryActions::Load);
}

TEST_CASE("Git window: one row or one commit reads in the singular")
{
	const std::optional<HistoryStatus> rows =
		DescribeHistory(HistoryStatusInput{.Shown = 1, .Complete = true, .FirstParentsOnly = true});
	const std::optional<HistoryStatus> commits =
		DescribeHistory(HistoryStatusInput{.Shown = 1, .Complete = true, .FirstParentsOnly = false});

	REQUIRE(rows.has_value());
	REQUIRE(commits.has_value());
	CHECK(rows->Text.starts_with("1 row · "));
	CHECK(commits->Text.starts_with("1 commit · "));
}

TEST_CASE("Git window: while a filter is on the footer says the graph is hidden and offers to search further")
{
	const std::optional<HistoryStatus> status =
		DescribeHistory(HistoryStatusInput{.Shown = 37, .Complete = false, .Filtered = true});

	REQUIRE(status.has_value());
	CHECK(status->Text == "Filtered: the graph is hidden while a filter is on");
	CHECK(status->Actions == HistoryActions::Search);
}

TEST_CASE("Git window: a filter that matched the whole history offers nothing more")
{
	const std::optional<HistoryStatus> status =
		DescribeHistory(HistoryStatusInput{.Shown = 58, .Complete = true, .Filtered = true});

	REQUIRE(status.has_value());
	CHECK(status->Text == "Filtered: the graph is hidden while a filter is on");
	CHECK(status->Actions == HistoryActions::None);
}

TEST_CASE("Git window: a filter that matches nothing still says so and offers to search further")
{
	const std::optional<HistoryStatus> status =
		DescribeHistory(HistoryStatusInput{.Shown = 0, .Complete = false, .Filtered = true});

	REQUIRE(status.has_value());
	CHECK(status->Actions == HistoryActions::Search);
}

TEST_CASE("Git window: while a query the reader asked for runs the footer says Loading in place of the buttons")
{
	const std::optional<HistoryStatus> status = DescribeHistory(HistoryStatusInput{
		.Shown = 500, .Complete = false, .FirstParentsOnly = false, .Asking = true, .AskedFiltered = false});

	REQUIRE(status.has_value());
	CHECK(status->Text == "500 commits · every commit with its graph");
	CHECK(status->Busy == "Loading\xE2\x80\xA6");
	CHECK(status->Actions == HistoryActions::None);
}

TEST_CASE("Git window: typing a filter over an unfiltered page says Searching while the search runs")
{
	const std::optional<HistoryStatus> status = DescribeHistory(HistoryStatusInput{
		.Shown = 500, .Complete = false, .FirstParentsOnly = false, .Asking = true, .AskedFiltered = true});

	REQUIRE(status.has_value());
	CHECK(status->Text == "500 commits · every commit with its graph");
	CHECK(status->Busy == "Searching\xE2\x80\xA6");
	CHECK(status->Actions == HistoryActions::None);
}

TEST_CASE("Git window: clearing a filter over a filtered page says Loading while the history is read again")
{
	const std::optional<HistoryStatus> status = DescribeHistory(HistoryStatusInput{
		.Shown = 37, .Complete = false, .Filtered = true, .Asking = true, .AskedFiltered = false});

	REQUIRE(status.has_value());
	CHECK(status->Text == "Filtered: the graph is hidden while a filter is on");
	CHECK(status->Busy == "Loading\xE2\x80\xA6");
	CHECK(status->Actions == HistoryActions::None);
}

TEST_CASE("Git window: while more can be loaded the footer says how many of the total are shown")
{
	const std::optional<HistoryStatus> rows = DescribeHistory(
		HistoryStatusInput{.Shown = 500, .Complete = false, .Total = 1739, .FirstParentsOnly = true});
	const std::optional<HistoryStatus> commits = DescribeHistory(
		HistoryStatusInput{.Shown = 500, .Complete = false, .Total = 1739, .FirstParentsOnly = false});

	REQUIRE(rows.has_value());
	REQUIRE(commits.has_value());
	CHECK(rows->Text == "500 of 1,739 rows · first parents only: each merge, squash or direct commit is one row");
	CHECK(commits->Text == "500 of 1,739 commits · every commit with its graph");
}

TEST_CASE("Git window: until the total is counted its place shows an ellipsis")
{
	const std::optional<HistoryStatus> status = DescribeHistory(
		HistoryStatusInput{.Shown = 500, .Complete = false, .Counting = true, .FirstParentsOnly = false});

	REQUIRE(status.has_value());
	CHECK(status->Text == "500 of … commits · every commit with its graph");
}

TEST_CASE("Git window: once everything is loaded the total is left out and the explanation stays")
{
	const std::optional<HistoryStatus> status = DescribeHistory(
		HistoryStatusInput{.Shown = 1739, .Complete = true, .Total = 1739, .FirstParentsOnly = true});

	REQUIRE(status.has_value());
	CHECK(status->Text == "1,739 rows · first parents only: each merge, squash or direct commit is one row");
}

TEST_CASE("Git window: the footer's noun follows the number it ends on")
{
	const std::optional<HistoryStatus> one = DescribeHistory(
		HistoryStatusInput{.Shown = 1, .Complete = false, .Total = 1, .FirstParentsOnly = false});
	const std::optional<HistoryStatus> ofMany = DescribeHistory(
		HistoryStatusInput{.Shown = 1, .Complete = false, .Total = 40, .FirstParentsOnly = true});

	REQUIRE(one.has_value());
	REQUIRE(ofMany.has_value());
	CHECK(one->Text.starts_with("1 of 1 commit · "));
	CHECK(ofMany->Text.starts_with("1 of 40 rows · "));
}

TEST_CASE("Git window: a filter hides the total as well")
{
	const std::optional<HistoryStatus> status = DescribeHistory(
		HistoryStatusInput{.Shown = 37, .Complete = false, .Filtered = true, .Total = 50000});

	REQUIRE(status.has_value());
	CHECK(status->Text == "Filtered: the graph is hidden while a filter is on");
}

TEST_CASE("A stash shows no line")
{
	CHECK_FALSE(DescribeHistory(HistoryStatusInput{.Shown = 1, .Complete = true, .Stash = true}).has_value());
}

TEST_CASE("An empty history without a filter shows no line")
{
	CHECK_FALSE(DescribeHistory(HistoryStatusInput{.Shown = 0, .Complete = true}).has_value());
}

TEST_CASE("While a query the reader started runs over a stash's page the line says Loading with nothing to count")
{
	const std::optional<HistoryStatus> status = DescribeHistory(
		HistoryStatusInput{.Shown = 1, .Complete = true, .Stash = true, .Asking = true});

	REQUIRE(status.has_value());
	CHECK(status->Text.empty());
	CHECK(status->Busy == "Loading\xE2\x80\xA6");
	CHECK(status->Actions == HistoryActions::None);
}

TEST_CASE("While a query the reader started runs over an empty page the line says Loading, or Searching for a filter")
{
	const std::optional<HistoryStatus> loading =
		DescribeHistory(HistoryStatusInput{.Shown = 0, .Complete = true, .Asking = true});
	const std::optional<HistoryStatus> searching = DescribeHistory(
		HistoryStatusInput{.Shown = 0, .Complete = true, .Asking = true, .AskedFiltered = true});

	REQUIRE(loading.has_value());
	CHECK(loading->Text.empty());
	CHECK(loading->Busy == "Loading\xE2\x80\xA6");
	REQUIRE(searching.has_value());
	CHECK(searching->Busy == "Searching\xE2\x80\xA6");
}
