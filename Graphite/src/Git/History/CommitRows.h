#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

namespace Graphite {

	enum class CommitRowKind
	{
		Commit,
		File,
		Separator
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

	std::size_t FileRowsOfExpanded(std::size_t fileCount);

	// A separator row's Commit is the commit it precedes, so it is never a commit's own row.
	CommitRows BuildCommitRows(std::size_t commitCount,
							   const std::function<std::size_t(std::size_t)>& fileRowsUnder,
							   std::optional<std::size_t> separatorBeforeCommit = std::nullopt);
}
