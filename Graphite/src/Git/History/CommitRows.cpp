#include "Git/History/CommitRows.h"

namespace Graphite {

	CommitRows BuildCommitRows(std::size_t commitCount,
							   const std::function<std::size_t(std::size_t)>& fileRowsUnder)
	{
		CommitRows rows;
		rows.RowOfCommit.reserve(commitCount);
		rows.Rows.reserve(commitCount);

		for (std::size_t commit = 0; commit < commitCount; ++commit)
		{
			rows.RowOfCommit.push_back(rows.Rows.size());

			CommitRow row;
			row.Kind = CommitRowKind::Commit;
			row.Commit = commit;
			rows.Rows.push_back(row);

			const std::size_t files = fileRowsUnder(commit);

			for (std::size_t file = 0; file < files; ++file)
			{
				CommitRow fileRow;
				fileRow.Kind = CommitRowKind::File;
				fileRow.Commit = commit;
				fileRow.File = file;
				rows.Rows.push_back(fileRow);
			}
		}

		return rows;
	}
}
