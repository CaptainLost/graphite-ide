#include <doctest/doctest.h>

#include "Git/History/CommitRows.h"

#include "GitFixtures.h"

#include "Core/Git/Status.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

using Graphite::CommitRowKind;
using Graphite::CommitRows;
using Graphite::ExtraRows;

namespace {

	std::function<std::size_t(std::size_t)> FileRowCountsFrom(std::vector<std::size_t> counts)
	{
		return [counts = std::move(counts)](std::size_t commit) {
			return commit < counts.size() ? counts[commit] : std::size_t{0};
		};
	}
}

TEST_CASE("No commits produce no rows")
{
	const CommitRows rows = Graphite::BuildCommitRows(0, FileRowCountsFrom({}));

	CHECK(rows.Rows.empty());
	CHECK(rows.RowOfCommit.empty());
}

TEST_CASE("Collapsed commits are one row each")
{
	const CommitRows rows = Graphite::BuildCommitRows(3, FileRowCountsFrom({0, 0, 0}));

	REQUIRE(rows.Rows.size() == 3);
	CHECK(rows.Rows[0].Kind == CommitRowKind::Commit);
	CHECK(rows.Rows[2].Commit == 2);

	REQUIRE(rows.RowOfCommit.size() == 3);
	CHECK(rows.RowOfCommit[0] == 0);
	CHECK(rows.RowOfCommit[1] == 1);
	CHECK(rows.RowOfCommit[2] == 2);
}

TEST_CASE("An expanded commit is followed by one row per file")
{
	const CommitRows rows = Graphite::BuildCommitRows(2, FileRowCountsFrom({3, 0}));

	REQUIRE(rows.Rows.size() == 5);

	CHECK(rows.Rows[0].Kind == CommitRowKind::Commit);
	CHECK(rows.Rows[1].Kind == CommitRowKind::File);
	CHECK(rows.Rows[1].Commit == 0);
	CHECK(rows.Rows[1].File == 0);
	CHECK(rows.Rows[3].File == 2);
	CHECK(rows.Rows[4].Kind == CommitRowKind::Commit);
	CHECK(rows.Rows[4].Commit == 1);
}

TEST_CASE("A commit's row moves down by the files above it, so the graph's dot for commit 1 is not on one of commit 0's files")
{
	const CommitRows rows = Graphite::BuildCommitRows(3, FileRowCountsFrom({2, 0, 1}));

	REQUIRE(rows.RowOfCommit.size() == 3);
	CHECK(rows.RowOfCommit[0] == 0);
	CHECK(rows.RowOfCommit[1] == 3);
	CHECK(rows.RowOfCommit[2] == 4);
}

TEST_CASE("Several commits can be expanded at once")
{
	const CommitRows rows = Graphite::BuildCommitRows(3, FileRowCountsFrom({1, 2, 1}));

	REQUIRE(rows.Rows.size() == 7);
	CHECK(rows.RowOfCommit[1] == 2);
	CHECK(rows.RowOfCommit[2] == 5);
}

TEST_CASE("An expanded commit whose diff has not arrived adds no rows because the caller reports zero for it")
{
	const CommitRows rows = Graphite::BuildCommitRows(2, FileRowCountsFrom({0, 0}));

	CHECK(rows.Rows.size() == 2);
	CHECK(rows.RowOfCommit[1] == 1);
}

TEST_CASE("Every row's commit index is in range and every commit has a row")
{
	const CommitRows rows = Graphite::BuildCommitRows(4, FileRowCountsFrom({2, 0, 3, 1}));

	for (const Graphite::CommitRow& row : rows.Rows)
	{
		CHECK(row.Commit < 4);
	}

	REQUIRE(rows.RowOfCommit.size() == 4);

	for (std::size_t commit = 0; commit < 4; ++commit)
	{
		const std::size_t row = rows.RowOfCommit[commit];

		REQUIRE(row < rows.Rows.size());
		CHECK(rows.Rows[row].Kind == CommitRowKind::Commit);
		CHECK(rows.Rows[row].Commit == commit);
	}
}

TEST_CASE("A fold row is a row of its own before the commit it precedes, and moves that commit down")
{
	const CommitRows rows = Graphite::BuildCommitRows(3, FileRowCountsFrom({0, 0, 0}), ExtraRows{.FoldBeforeCommit = 2});

	REQUIRE(rows.Rows.size() == 4);
	CHECK(rows.Rows[2].Kind == CommitRowKind::Fold);
	CHECK(rows.Rows[3].Kind == CommitRowKind::Commit);
	CHECK(rows.Rows[3].Commit == 2);

	CHECK(rows.RowOfCommit[1] == 1);
	CHECK(rows.RowOfCommit[2] == 3);
}

TEST_CASE("A fold row before the first commit is the top row")
{
	const CommitRows rows = Graphite::BuildCommitRows(2, FileRowCountsFrom({0, 0}), ExtraRows{.FoldBeforeCommit = 0});

	REQUIRE(rows.Rows.size() == 3);
	CHECK(rows.Rows[0].Kind == CommitRowKind::Fold);
	CHECK(rows.RowOfCommit[0] == 1);
}

TEST_CASE("A fold row after an expanded commit comes after that commit's file rows")
{
	const CommitRows rows = Graphite::BuildCommitRows(2, FileRowCountsFrom({2, 0}), ExtraRows{.FoldBeforeCommit = 1});

	REQUIRE(rows.Rows.size() == 5);
	CHECK(rows.Rows[3].Kind == CommitRowKind::Fold);
	CHECK(rows.RowOfCommit[1] == 4);
}

TEST_CASE("Without extra rows every row is a commit or a file")
{
	const CommitRows rows = Graphite::BuildCommitRows(2, FileRowCountsFrom({1, 0}));

	for (const Graphite::CommitRow& row : rows.Rows)
	{
		CHECK((row.Kind == CommitRowKind::Commit || row.Kind == CommitRowKind::File));
	}
}

TEST_CASE("Git window: the local changes row comes first and shifts every commit row")
{
	const CommitRows rows = Graphite::BuildCommitRows(3, FileRowCountsFrom({1, 0, 0}), ExtraRows{.LocalChanges = true});

	REQUIRE(rows.Rows.size() == 5);
	CHECK(rows.Rows[0].Kind == CommitRowKind::LocalChanges);
	CHECK(rows.Rows[1].Kind == CommitRowKind::Commit);
	CHECK(rows.Rows[2].Kind == CommitRowKind::File);

	REQUIRE(rows.RowOfCommit.size() == 3);
	CHECK(rows.RowOfCommit[0] == 1);
	CHECK(rows.RowOfCommit[1] == 3);
	CHECK(rows.RowOfCommit[2] == 4);
}

TEST_CASE("Git window: the local changes row and a fold row together each take a row")
{
	const CommitRows rows =
		Graphite::BuildCommitRows(3, FileRowCountsFrom({0, 0, 0}), ExtraRows{.LocalChanges = true, .FoldBeforeCommit = 1});

	REQUIRE(rows.Rows.size() == 5);
	CHECK(rows.Rows[0].Kind == CommitRowKind::LocalChanges);
	CHECK(rows.Rows[2].Kind == CommitRowKind::Fold);
	CHECK(rows.RowOfCommit[0] == 1);
	CHECK(rows.RowOfCommit[1] == 3);
}

TEST_CASE("Git window: with local changes and no commits one note row stands for the commits")
{
	const CommitRows rows = Graphite::BuildCommitRows(0, FileRowCountsFrom({}), ExtraRows{.LocalChanges = true});

	REQUIRE(rows.Rows.size() == 2);
	CHECK(rows.Rows[0].Kind == CommitRowKind::LocalChanges);
	CHECK(rows.Rows[1].Kind == CommitRowKind::Note);
	CHECK(rows.RowOfCommit.empty());
}

TEST_CASE("Git window: a waiting history without local changes is one note row")
{
	const CommitRows rows = Graphite::BuildCommitRows(0, FileRowCountsFrom({}), ExtraRows{.Waiting = true});

	REQUIRE(rows.Rows.size() == 1);
	CHECK(rows.Rows[0].Kind == CommitRowKind::Note);

	CHECK(Graphite::BuildCommitRows(0, FileRowCountsFrom({})).Rows.empty());
}

TEST_CASE("Git window: local changes are described in words")
{
	CHECK(Graphite::DescribeLocalChanges(nullptr) == "\xE2\x80\xA6");

	const Core::WorkingTreeStatus clean;
	CHECK(Graphite::DescribeLocalChanges(&clean) == "nothing to commit");

	Core::FileStatus staged{.Path = "a.cpp", .Index = Core::StatusFlags::Modified};
	Core::FileStatus both{.Path = "b.cpp", .Index = Core::StatusFlags::Modified, .WorkTree = Core::StatusFlags::Modified};
	Core::FileStatus edited{.Path = "c.cpp", .WorkTree = Core::StatusFlags::Modified};
	Core::FileStatus untracked{.Path = "d.cpp", .WorkTree = Core::StatusFlags::New, .IsUntracked = true};

	const Core::WorkingTreeStatus status = WorkingTreeStatusOf({staged, both, edited, untracked});
	CHECK(Graphite::DescribeLocalChanges(&status) == "2 staged \xC2\xB7 3 not staged");

	Core::FileStatus conflicted{.Path = "e.cpp", .IsConflicted = true};
	const Core::WorkingTreeStatus merging = WorkingTreeStatusOf({conflicted, edited});
	CHECK(Graphite::DescribeLocalChanges(&merging) == "1 conflicted \xC2\xB7 0 staged \xC2\xB7 1 not staged");
}

TEST_CASE("An expanded commit always gets at least one file row, since zero files is both a diff still being fetched and a commit that changed nothing")
{
	CHECK(Graphite::FileRowsOfExpanded(0) == 1);
	CHECK(Graphite::FileRowsOfExpanded(1) == 1);
	CHECK(Graphite::FileRowsOfExpanded(7) == 7);
}

TEST_CASE("An expanded commit with no files is a commit row and one placeholder file row")
{
	const CommitRows rows = Graphite::BuildCommitRows(2, [](std::size_t commit) {
		return commit == 0 ? Graphite::FileRowsOfExpanded(0) : std::size_t{0};
	});

	REQUIRE(rows.Rows.size() == 3);
	CHECK(rows.Rows[1].Kind == CommitRowKind::File);
	CHECK(rows.Rows[1].Commit == 0);
	CHECK(rows.Rows[1].File == 0);
	CHECK(rows.Rows[2].Kind == CommitRowKind::Commit);
	CHECK(rows.RowOfCommit[1] == 2);
}

TEST_CASE("Git window: brought commits are listed under their merge")
{
	ExtraRows extra;
	extra.BroughtRowsUnder = [](std::size_t commit) { return commit == 0 ? std::size_t{3} : std::size_t{0}; };

	const CommitRows rows = Graphite::BuildCommitRows(2, FileRowCountsFrom({0, 0}), extra);

	REQUIRE(rows.Rows.size() == 5);
	CHECK(rows.Rows[0].Kind == CommitRowKind::Commit);
	CHECK(rows.Rows[1].Kind == CommitRowKind::Brought);
	CHECK(rows.Rows[1].Commit == 0);
	CHECK(rows.Rows[1].File == 0);
	CHECK(rows.Rows[3].File == 2);
	CHECK(rows.Rows[4].Kind == CommitRowKind::Commit);
	CHECK(rows.RowOfCommit[1] == 4);

	// Before the commits are read, one row stands for them.
	CHECK(Graphite::BroughtRowsOf(nullptr) == 1);

	Core::LogPage none;
	CHECK(Graphite::BroughtRowsOf(&none) == 1);
}

TEST_CASE("Git window: a merge row counts its commits once opened")
{
	Core::LogPage page;
	page.Commits.resize(3);
	CHECK(Graphite::DescribeBrought(page) == "3 commits");

	page.Commits.resize(1);
	CHECK(Graphite::DescribeBrought(page) == "1 commit");

	page.Commits.resize(500);
	page.Complete = false;
	CHECK(Graphite::DescribeBrought(page) == "500+ commits");

	Core::CommitSummary merge;
	merge.Parents.resize(2);
	CHECK(Graphite::IsMerge(merge));
	merge.Parents.resize(1);
	CHECK_FALSE(Graphite::IsMerge(merge));
}
