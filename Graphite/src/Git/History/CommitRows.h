#pragma once

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

		// Stands for the commits while there are none to draw under the Local changes row: the wait for them, or the
		// word that there are none.
		Note
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
		std::optional<std::size_t> FoldBeforeCommit; // just above that commit
	};

	std::size_t FileRowsOfExpanded(std::size_t fileCount);

	// A fold row's Commit is the commit it precedes, so it is never a commit's own row.
	CommitRows BuildCommitRows(std::size_t commitCount,
							   const std::function<std::size_t(std::size_t)>& fileRowsUnder,
							   const ExtraRows& extra = {});

	// What the Local changes row says after its name: "2 staged · 3 not staged", "nothing to commit", or an ellipsis
	// while the status is not read.
	std::string DescribeLocalChanges(const Core::WorkingTreeStatus* status);
}
