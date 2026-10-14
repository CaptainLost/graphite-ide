#include <doctest/doctest.h>

#include "Git/History/CommitRows.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

using Graphite::CommitRowKind;
using Graphite::CommitRows;

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

TEST_CASE("A separator is a row of its own before the commit it precedes, and moves that commit down")
{
	const CommitRows rows = Graphite::BuildCommitRows(3, FileRowCountsFrom({0, 0, 0}), 2);

	REQUIRE(rows.Rows.size() == 4);
	CHECK(rows.Rows[2].Kind == CommitRowKind::Separator);
	CHECK(rows.Rows[3].Kind == CommitRowKind::Commit);
	CHECK(rows.Rows[3].Commit == 2);

	CHECK(rows.RowOfCommit[1] == 1);
	CHECK(rows.RowOfCommit[2] == 3);
}

TEST_CASE("A separator before the first commit is the top row")
{
	const CommitRows rows = Graphite::BuildCommitRows(2, FileRowCountsFrom({0, 0}), 0);

	REQUIRE(rows.Rows.size() == 3);
	CHECK(rows.Rows[0].Kind == CommitRowKind::Separator);
	CHECK(rows.RowOfCommit[0] == 1);
}

TEST_CASE("A separator after an expanded commit comes after that commit's file rows")
{
	const CommitRows rows = Graphite::BuildCommitRows(2, FileRowCountsFrom({2, 0}), 1);

	REQUIRE(rows.Rows.size() == 5);
	CHECK(rows.Rows[3].Kind == CommitRowKind::Separator);
	CHECK(rows.RowOfCommit[1] == 4);
}

TEST_CASE("Without a separator every row is a commit or a file")
{
	const CommitRows rows = Graphite::BuildCommitRows(2, FileRowCountsFrom({1, 0}), std::nullopt);

	for (const Graphite::CommitRow& row : rows.Rows)
	{
		CHECK(row.Kind != CommitRowKind::Separator);
	}
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