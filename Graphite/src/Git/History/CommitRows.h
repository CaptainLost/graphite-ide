#pragma once

#include <cstddef>
#include <functional>
#include <vector>

namespace Graphite {

	enum class CommitRowKind
	{
		Commit,
		File
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

	CommitRows BuildCommitRows(std::size_t commitCount,
							   const std::function<std::size_t(std::size_t)>& fileRowsUnder);
}
