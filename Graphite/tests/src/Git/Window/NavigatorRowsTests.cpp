#include <doctest/doctest.h>

#include "GitFixtures.h"
#include "Git/Window/NavigatorRows.h"

#include <cstddef>
#include <string>
#include <vector>

namespace {

	using namespace Graphite;

	Core::Branch LocalBranch(const std::string& name, long long tipSeconds, bool isHead = false)
	{
		Core::Branch branch;
		branch.Name = name;
		branch.FullName = "refs/heads/" + name;
		branch.IsHead = isHead;

		Core::CommitSummary tip;
		tip.Committer = Core::Signature{.Name = "Ann", .Email = "ann@example.com", .When = SystemSecondsAt(tipSeconds)};
		branch.TipCommit = tip;

		return branch;
	}

	WorkCard Thread(const std::string& name)
	{
		WorkCard card;
		card.Kind = WorkCardKind::Branch;
		card.Branch = "refs/heads/" + name;
		return card;
	}

	WorkBoard BoardWithThreads(std::size_t threads)
	{
		WorkBoard board;

		WorkCard workingCopy;
		workingCopy.Kind = WorkCardKind::WorkingCopy;
		workingCopy.Branch = "refs/heads/main";
		board.Cards.push_back(workingCopy);

		for (std::size_t thread = 0; thread < threads; ++thread)
		{
			board.Cards.push_back(Thread("thread" + std::to_string(thread)));
		}

		return board;
	}

	std::size_t CountOf(const std::vector<NavigatorRow>& rows, NavigatorRowKind kind)
	{
		std::size_t count = 0;

		for (const NavigatorRow& row : rows)
		{
			count += row.Kind == kind ? 1 : 0;
		}

		return count;
	}

	const NavigatorRow* FirstOf(const std::vector<NavigatorRow>& rows, NavigatorRowKind kind)
	{
		for (const NavigatorRow& row : rows)
		{
			if (row.Kind == kind)
			{
				return &row;
			}
		}

		return nullptr;
	}
}

TEST_CASE("Work lists at most six threads and then Show all with their number")
{
	const WorkBoard board = BoardWithThreads(9);
	const std::vector<Core::Branch> branches;

	const std::vector<NavigatorRow> rows =
		BuildNavigatorRows({.Board = &board, .ChangeCount = std::nullopt, .Branches = &branches, .Selection = {}});

	CHECK(CountOf(rows, NavigatorRowKind::Thread) == kNavigatorThreads);

	const NavigatorRow* showAll = FirstOf(rows, NavigatorRowKind::ShowAllWork);
	REQUIRE(showAll != nullptr);
	CHECK(showAll->Count == "9");

	const WorkBoard fewer = BoardWithThreads(6);
	const std::vector<NavigatorRow> fewerRows =
		BuildNavigatorRows({.Board = &fewer, .ChangeCount = std::nullopt, .Branches = &branches, .Selection = {}});

	CHECK(CountOf(fewerRows, NavigatorRowKind::Thread) == 6);
	CHECK(FirstOf(fewerRows, NavigatorRowKind::ShowAllWork) == nullptr);
}

TEST_CASE("The working copy is not a thread row")
{
	const WorkBoard board = BoardWithThreads(2);
	const std::vector<Core::Branch> branches;

	const std::vector<NavigatorRow> rows =
		BuildNavigatorRows({.Board = &board, .ChangeCount = std::nullopt, .Branches = &branches, .Selection = {}});

	REQUIRE(CountOf(rows, NavigatorRowKind::Thread) == 2);

	for (const NavigatorRow& row : rows)
	{
		if (row.Kind == NavigatorRowKind::Thread)
		{
			CHECK(row.Index != 0);
			CHECK(row.Branch != "refs/heads/main");
		}
	}

	CHECK(rows.front().Kind == NavigatorRowKind::Work);
}

TEST_CASE("Ready and locked rows appear only when there are some")
{
	WorkBoard board = BoardWithThreads(1);
	const std::vector<Core::Branch> branches;

	const NavigatorInput none{.Board = &board, .ChangeCount = std::nullopt, .Branches = &branches, .Selection = {}};
	CHECK(FirstOf(BuildNavigatorRows(none), NavigatorRowKind::Ready) == nullptr);
	CHECK(FirstOf(BuildNavigatorRows(none), NavigatorRowKind::Locked) == nullptr);

	board.Ready.resize(3);
	board.Info.push_back(WorkInfo{.Kind = WorkInfoKind::Locked});
	board.Info.push_back(WorkInfo{.Kind = WorkInfoKind::FolderMissing});

	const std::vector<NavigatorRow> rows = BuildNavigatorRows(none);

	const NavigatorRow* ready = FirstOf(rows, NavigatorRowKind::Ready);
	const NavigatorRow* locked = FirstOf(rows, NavigatorRowKind::Locked);
	REQUIRE(ready != nullptr);
	REQUIRE(locked != nullptr);
	CHECK(ready->Count == "3");
	CHECK(ready->Label == "ready to remove");
	CHECK(locked->Count == "1");
	CHECK(locked->Label == "locked by another session");
}

TEST_CASE("Branches lists HEAD's first and then the newest, then Show all with the total")
{
	std::vector<Core::Branch> branches;

	for (int index = 0; index < 9; ++index)
	{
		branches.push_back(LocalBranch("b" + std::to_string(index), 1000 + index * 10, index == 2));
	}

	Core::Branch remote = LocalBranch("origin/new", 99999);
	remote.Kind = Core::RefKind::Remote;
	remote.FullName = "refs/remotes/origin/new";
	branches.push_back(remote);

	const std::vector<std::size_t> newest = NewestLocalBranches(branches, kNavigatorBranches);

	REQUIRE(newest.size() == 6);
	CHECK(newest[0] == 2);
	CHECK(newest[1] == 8);
	CHECK(newest[2] == 7);
	CHECK(newest[5] == 4);

	const WorkBoard board = BoardWithThreads(0);
	const std::vector<NavigatorRow> rows =
		BuildNavigatorRows({.Board = &board, .ChangeCount = std::nullopt, .Branches = &branches, .Selection = {}});

	CHECK(CountOf(rows, NavigatorRowKind::Branch) == 6);

	const NavigatorRow* showAll = FirstOf(rows, NavigatorRowKind::ShowAllBranches);
	REQUIRE(showAll != nullptr);
	CHECK(showAll->Count == "9");
	CHECK(rows.back().Kind == NavigatorRowKind::ShowAllBranches);
}

TEST_CASE("The selected view's row is marked")
{
	const WorkBoard board = BoardWithThreads(2);
	const std::vector<Core::Branch> branches = {LocalBranch("main", 10, true), LocalBranch("topic", 20)};

	const auto selectedKinds = [&](GitWindowSelection selection)
	{
		std::vector<NavigatorRowKind> kinds;

		for (const NavigatorRow& row :
			 BuildNavigatorRows({.Board = &board, .ChangeCount = 1, .Branches = &branches, .Selection = selection}))
		{
			if (row.Selected)
			{
				kinds.push_back(row.Kind);
			}
		}

		return kinds;
	};

	CHECK(selectedKinds({GitWindowView::Work, ""}) == std::vector{NavigatorRowKind::Work});
	CHECK(selectedKinds({GitWindowView::Changes, ""}) == std::vector{NavigatorRowKind::Changes});
	CHECK(selectedKinds({GitWindowView::Branches, ""}) == std::vector{NavigatorRowKind::ShowAllBranches});
	CHECK(selectedKinds({GitWindowView::Thread, "refs/heads/thread1"}) == std::vector{NavigatorRowKind::Thread});
	CHECK(selectedKinds({GitWindowView::History, "refs/heads/topic"}) == std::vector{NavigatorRowKind::Branch});
	CHECK(selectedKinds({GitWindowView::History, "refs/heads/gone"}).empty());
}

TEST_CASE("Changes shows its count once it is known")
{
	const WorkBoard board = BoardWithThreads(0);
	const std::vector<Core::Branch> branches;

	const std::vector<NavigatorRow> unknown =
		BuildNavigatorRows({.Board = &board, .ChangeCount = std::nullopt, .Branches = &branches, .Selection = {}});
	const std::vector<NavigatorRow> zero =
		BuildNavigatorRows({.Board = &board, .ChangeCount = 0, .Branches = &branches, .Selection = {}});
	const std::vector<NavigatorRow> four =
		BuildNavigatorRows({.Board = &board, .ChangeCount = 4, .Branches = &branches, .Selection = {}});

	CHECK(FirstOf(unknown, NavigatorRowKind::Changes)->Count.empty());
	CHECK(FirstOf(zero, NavigatorRowKind::Changes)->Count == "0");
	CHECK(FirstOf(four, NavigatorRowKind::Changes)->Count == "4");
}

TEST_CASE("A thread row is labelled by its worktree folder, else its short branch name")
{
	WorkCard card = Thread("topic");
	CHECK(ThreadRowLabel(card) == "topic");

	card.Worktree = std::filesystem::path{"C:/work/topic-wt"};
	CHECK(ThreadRowLabel(card) == "topic-wt");
}

TEST_CASE("A detached worktree's row selects Work")
{
	NavigatorRow row;
	row.Kind = NavigatorRowKind::Thread;
	CHECK(SelectionOf(row) == GitWindowSelection{GitWindowView::Work, ""});

	row.Branch = "refs/heads/topic";
	CHECK(SelectionOf(row) == GitWindowSelection{GitWindowView::Thread, "refs/heads/topic"});
}
