#include <doctest/doctest.h>

#include "Git/Lines/HeadInRead.h"

#include "Core/Git/Head.h"

#include <string>
#include <utility>

using Graphite::HeadInRead;
using Graphite::HeadInReadInput;
using Graphite::HistoryRows;
using Graphite::HistoryRowsInput;
using Graphite::HistoryWaitInput;
using Graphite::HistoryWaitsForLines;
using Graphite::PlaceHeadInRead;
using Graphite::RowsOfHistory;

namespace {

	Core::HeadState OnBranch(std::string name)
	{
		Core::HeadState head;
		head.Kind = Core::HeadKind::Branch;
		head.BranchName = std::move(name);
		return head;
	}

	Core::HeadState Detached()
	{
		Core::HeadState head;
		head.Kind = Core::HeadKind::Detached;
		return head;
	}

	Core::HeadState Unborn(std::string name)
	{
		Core::HeadState head;
		head.Kind = Core::HeadKind::Unborn;
		head.BranchName = std::move(name);
		return head;
	}

	constexpr bool kReadComing = true;
	constexpr bool kNoReadComing = false;
}

TEST_CASE("Git window: HEAD's history takes the read's line while the read names HEAD's branch checked out")
{
	const Core::HeadState head = OnBranch("main");

	CHECK(PlaceHeadInRead(HeadInReadInput{.Head = &head, .ReadCheckedOut = "refs/heads/main", .ReadComing = kNoReadComing}) ==
		  HeadInRead::Agrees);
	CHECK(PlaceHeadInRead(HeadInReadInput{.Head = &head, .ReadCheckedOut = "refs/heads/main", .ReadComing = kReadComing}) ==
		  HeadInRead::Agrees);
}

TEST_CASE("Git window: HEAD's history waits for the read coming after a checkout")
{
	const Core::HeadState head = OnBranch("feature");

	CHECK(PlaceHeadInRead(HeadInReadInput{.Head = &head, .ReadCheckedOut = "refs/heads/main", .ReadComing = kReadComing}) ==
		  HeadInRead::WaitsForRead);
}

TEST_CASE("Git window: HEAD's history is shown alone when the read names another branch and none is coming")
{
	const Core::HeadState head = OnBranch("feature");

	CHECK(PlaceHeadInRead(HeadInReadInput{.Head = &head, .ReadCheckedOut = "refs/heads/main", .ReadComing = kNoReadComing}) ==
		  HeadInRead::Differs);
}

TEST_CASE("Git window: a detached HEAD agrees only with a read that names no branch checked out")
{
	const Core::HeadState head = Detached();

	CHECK(PlaceHeadInRead(HeadInReadInput{.Head = &head, .ReadCheckedOut = "", .ReadComing = kNoReadComing}) == HeadInRead::Agrees);
	CHECK(PlaceHeadInRead(HeadInReadInput{.Head = &head, .ReadCheckedOut = "refs/heads/main", .ReadComing = kReadComing}) ==
		  HeadInRead::WaitsForRead);
}

TEST_CASE("Git window: an unborn HEAD agrees with a read that lists no branch checked out")
{
	const Core::HeadState head = Unborn("main");

	CHECK(PlaceHeadInRead(HeadInReadInput{.Head = &head, .ReadCheckedOut = "", .ReadComing = kNoReadComing}) == HeadInRead::Agrees);
}

TEST_CASE("Git window: with HEAD unread the read's checked-out branch stands")
{
	CHECK(PlaceHeadInRead(HeadInReadInput{.Head = nullptr, .ReadCheckedOut = "refs/heads/main", .ReadComing = kReadComing}) ==
		  HeadInRead::Agrees);
}

TEST_CASE("Git window: HEAD's history stays shown while a retry after a failure runs")
{
	const Core::HeadState head = OnBranch("feature");

	// The read that failed and the refresh listing the branches for its retry are both on their way.
	CHECK(PlaceHeadInRead(HeadInReadInput{.Head = &head,
										  .ReadCheckedOut = "refs/heads/main",
										  .ReadComing = kReadComing,
										  .ReadFailed = true}) == HeadInRead::Differs);
}

TEST_CASE("Git window: a history waits for a first read that is coming and not for one that cannot come")
{
	CHECK(HistoryWaitsForLines(HistoryWaitInput{.HasRead = false, .Reading = true}));
	CHECK(HistoryWaitsForLines(HistoryWaitInput{.HasRead = false, .Reading = false}));
	CHECK_FALSE(HistoryWaitsForLines(HistoryWaitInput{.HasRead = false, .BranchesFailed = true}));
}

TEST_CASE("Git window: a history waits for a read that may hold its reference and for HEAD's read after a checkout")
{
	CHECK(HistoryWaitsForLines(HistoryWaitInput{.HasRead = true, .Reading = true, .ReadLacksShown = true}));
	CHECK_FALSE(HistoryWaitsForLines(HistoryWaitInput{.HasRead = true, .Reading = false, .ReadLacksShown = true}));
	CHECK_FALSE(HistoryWaitsForLines(HistoryWaitInput{.HasRead = true, .Reading = true, .ReadLacksShown = false}));
	CHECK(HistoryWaitsForLines(HistoryWaitInput{.HasRead = true, .Head = HeadInRead::WaitsForRead}));
	CHECK_FALSE(HistoryWaitsForLines(HistoryWaitInput{.HasRead = true, .Head = HeadInRead::Differs}));
}

TEST_CASE("Git window: a retry after a failed read keeps the history from waiting")
{
	// No read in hand: the first read failed and is asked again.
	CHECK_FALSE(HistoryWaitsForLines(HistoryWaitInput{.HasRead = false, .Reading = true, .ReadFailed = true}));

	// A read in hand that lacks the reference shown, and a later read that failed and is asked again.
	CHECK_FALSE(HistoryWaitsForLines(
		HistoryWaitInput{.HasRead = true, .Reading = true, .ReadFailed = true, .ReadLacksShown = true}));
}

TEST_CASE("Git window: HEAD's rows shown while the read failed stay when a read names its branch until the page asked with its line is in")
{
	// The read succeeds after a failure and names HEAD's branch: the rows shown with no line stay, with nothing of the
	// line, while the page asked with it is read.
	CHECK(RowsOfHistory(HistoryRowsInput{.ShownWithNoLine = true}) == HistoryRows::Kept);

	// That page lands with its counts: its rows take their place in that frame.
	CHECK(RowsOfHistory(HistoryRowsInput{.ShownWithNoLine = true, .FirstRowsReady = true}) == HistoryRows::Shown);
}

TEST_CASE("Git window: a history's first rows wait for their page unless rows of the same branch are on screen")
{
	CHECK(RowsOfHistory(HistoryRowsInput{}) == HistoryRows::Waiting);
	CHECK(RowsOfHistory(HistoryRowsInput{.FirstRowsReady = true}) == HistoryRows::Shown);
	CHECK(RowsOfHistory(HistoryRowsInput{.ShownForThis = true}) == HistoryRows::Shown);
}
