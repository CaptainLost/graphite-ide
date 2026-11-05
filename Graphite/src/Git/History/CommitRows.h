#pragma once

#include "Core/Git/Commit.h"
#include "Core/Git/Status.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	enum class CommitRowKind
	{
		Commit,
		File,
		LocalChanges,
		Fold,

		// A commit a merge brought, or the one row standing for them while there are none to list. Commit is the merge's
		// index, File the place among the commits it brought.
		Brought,

		// Stands for the commits while there are none to draw: the wait for them, or, under the Local changes row, the
		// word that there are none.
		Note,

		// The sentence about the shown reference, under the Local changes row, so that row never moves when it comes.
		Banner
	};

	struct CommitRow
	{
		CommitRowKind Kind = CommitRowKind::Commit;
		std::size_t Commit = 0;
		std::size_t File = 0;
	};

	struct CommitRows
	{
		std::vector<CommitRow> Rows;
		std::vector<std::size_t> RowOfCommit;
	};

	// The rows the history has besides its commits and their files.
	struct ExtraRows
	{
		bool LocalChanges = false;                   // first, above every commit
		bool Banner = false;                         // under the Local changes row, above every commit
		std::optional<std::size_t> FoldBeforeCommit; // just above that commit
		bool Waiting = false;                        // the commits are on their way

		// How many rows stand under a commit for what it brought; empty or 0 for none.
		std::function<std::size_t(std::size_t)> BroughtRowsUnder;
	};

	std::size_t FileRowsOfExpanded(std::size_t fileCount);

	bool IsMerge(const Core::CommitSummary& commit);

	// An opened merge lists the commits it brought; before they are read, or when there are none or they could not be
	// read, one row stands in their place.
	std::size_t BroughtRowsOf(const Core::LogPage* brought);

	// "3 commits", "1 commit", "500+ commits" when the page stops short of what the merge brought.
	std::string DescribeBrought(const Core::LogPage& brought);

	// A fold row's Commit is the commit it precedes, so it is never a commit's own row.
	CommitRows BuildCommitRows(std::size_t commitCount,
							   const std::function<std::size_t(std::size_t)>& fileRowsUnder,
							   const ExtraRows& extra = {});

	// What the Local changes row says after its name: "2 staged · 3 not staged", "nothing to commit", or an ellipsis
	// while the status is not read.
	std::string DescribeLocalChanges(const Core::WorkingTreeStatus* status);
}
