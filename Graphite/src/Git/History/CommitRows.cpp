#include "Git/History/CommitRows.h"

namespace Graphite {

	namespace {

		constexpr std::size_t kPlaceholderFileRowCount = 1;
	}

	std::size_t FileRowsOfExpanded(std::size_t fileCount)
	{
		return fileCount == 0 ? kPlaceholderFileRowCount : fileCount;
	}

	CommitRows BuildCommitRows(std::size_t commitCount,
							   const std::function<std::size_t(std::size_t)>& fileRowsUnder,
							   std::optional<std::size_t> separatorBeforeCommit)
	{
		CommitRows rows;
		rows.RowOfCommit.reserve(commitCount);
		rows.Rows.reserve(commitCount + 1);

		for (std::size_t commit = 0; commit < commitCount; ++commit)
		{
			if (separatorBeforeCommit == commit)
			{
				CommitRow separator;
				separator.Kind = CommitRowKind::Separator;
				separator.Commit = commit;
				rows.Rows.push_back(separator);
			}

			rows.RowOfCommit.push_back(rows.Rows.size());

			CommitRow row;
			row.Kind = CommitRowKind::Commit;
			row.Commit = commit;
			rows.Rows.push_back(row);

			const std::size_t fileRowCount = fileRowsUnder(commit);

			for (std::size_t file = 0; file < fileRowCount; ++file)
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
