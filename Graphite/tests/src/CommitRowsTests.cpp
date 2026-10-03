#include <doctest/doctest.h>

#include "Git/History/CommitRows.h"

#include <vector>

using Graphite::CommitRowKind;
using Graphite::CommitRows;

namespace {

	// A commit's file-row count taken from a table, so a test reads as the shape it means.
	auto From(std::vector<std::size_t> counts)
	{
		return [counts = std::move(counts)](std::size_t commit) {
			return commit < counts.size() ? counts[commit] : 0;
		};
	}
}

TEST_CASE("No commits produce no rows")
{
	const CommitRows rows = Graphite::BuildCommitRows(0, From({}));

	CHECK(rows.Rows.empty());
	CHECK(rows.RowOfCommit.empty());
}

TEST_CASE("Collapsed commits are one row each")
{
	const CommitRows rows = Graphite::BuildCommitRows(3, From({0, 0, 0}));

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
	const CommitRows rows = Graphite::BuildCommitRows(2, From({3, 0}));

	REQUIRE(rows.Rows.size() == 5);

	CHECK(rows.Rows[0].Kind == CommitRowKind::Commit);
	CHECK(rows.Rows[1].Kind == CommitRowKind::File);
	CHECK(rows.Rows[1].Commit == 0);
	CHECK(rows.Rows[1].File == 0);
	CHECK(rows.Rows[3].File == 2);
	CHECK(rows.Rows[4].Kind == CommitRowKind::Commit);
	CHECK(rows.Rows[4].Commit == 1);
}

TEST_CASE("A commit's row moves down by the files above it")
{
	const CommitRows rows = Graphite::BuildCommitRows(3, From({2, 0, 1}));

	// This is the mapping the graph needs: commit 1 is the fourth row, not the second,
	// and a graph drawn at row 1 would put its dot on one of commit 0's files.
	REQUIRE(rows.RowOfCommit.size() == 3);
	CHECK(rows.RowOfCommit[0] == 0);
	CHECK(rows.RowOfCommit[1] == 3);
	CHECK(rows.RowOfCommit[2] == 4);
}

TEST_CASE("Several commits can be expanded at once")
{
	const CommitRows rows = Graphite::BuildCommitRows(3, From({1, 2, 1}));

	REQUIRE(rows.Rows.size() == 7);
	CHECK(rows.RowOfCommit[1] == 2);
	CHECK(rows.RowOfCommit[2] == 5);
}

TEST_CASE("An expanded commit whose diff has not arrived adds no rows")
{
	// The caller reports zero for a commit it is still waiting on, so the row layout is
	// the same as a collapsed one — the row simply fills in when the answer lands.
	const CommitRows rows = Graphite::BuildCommitRows(2, From({0, 0}));

	CHECK(rows.Rows.size() == 2);
	CHECK(rows.RowOfCommit[1] == 1);
}

TEST_CASE("Every row's commit index is in range and every commit has a row")
{
	const CommitRows rows = Graphite::BuildCommitRows(4, From({2, 0, 3, 1}));

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
