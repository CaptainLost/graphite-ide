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

		// Index into the page's commits. Set for both kinds — a file row belongs to the
		// commit it sits under.
		std::size_t Commit = 0;

		// Index into that commit's diff. Meaningless for a commit row.
		std::size_t File = 0;
	};

	struct CommitRows
	{
		std::vector<CommitRow> Rows;

		// Where each commit's own row ended up. The graph draws one dot per commit and
		// needs this, because with files interleaved a commit is no longer at the row its
		// index names.
		std::vector<std::size_t> RowOfCommit;
	};

	// How many file rows an expanded commit gets when its diff holds `files` of them. At
	// least one: a commit that changed nothing, and one whose diff has not arrived, would
	// otherwise open onto nothing and look as though the arrow had done nothing. That row is
	// a placeholder saying which of the cases it is: waiting, failed, or nothing changed.
	std::size_t FileRowsOfExpanded(std::size_t files);

	// `fileRowsUnder(commit)` answers how many file rows follow that commit: zero when it
	// is collapsed, and FileRowsOfExpanded of its diff once it is open. Taking a function
	// rather than the page and the diffs keeps this file free of every git type, and makes
	// its tests a handful of integers.
	CommitRows BuildCommitRows(std::size_t commitCount,
							   const std::function<std::size_t(std::size_t)>& fileRowsUnder);
}
